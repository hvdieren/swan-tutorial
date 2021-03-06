// -*- c++ -*-

// TODO before release
// * get fixed queue size right and remove % if possible (when elms are not 2**k)
// * validate on ferret, dedup, bzip2 (scrambled)

#ifndef QUEUE_FIXED_SIZE_QUEUE_H
#define QUEUE_FIXED_SIZE_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <utility> // std::move, std::forward
#include "swan/alc_allocator.h"
#include "swan/alc_mmappol.h"
#include "swan/alc_flpol.h"
#include "swan/padding.h"

#if PROFILE_QUEUE
#include "swan/../util/pp_time.h"
#endif // PROFILE_QUEUE

namespace obj {

#if PROFILE_QUEUE
struct profile_queue {
    pp_time_t sq_await;
    pp_time_t sq_peek;
    pp_time_t qv_qhead;
    pp_time_t qs_peek;
    pp_time_t qs_pop;

    size_t num_segment_alloc;
    size_t num_segment_dealloc;
    size_t num_tail_wrap;

    profile_queue() : num_segment_alloc( 0 ), num_segment_dealloc( 0 ) {
	memset( &sq_await, 0, sizeof(sq_await) );
	memset( &sq_peek, 0, sizeof(sq_peek) );
	memset( &qs_peek, 0, sizeof(qs_peek) );
	memset( &qs_pop, 0, sizeof(qs_pop) );
	memset( &qv_qhead, 0, sizeof(qv_qhead) );
    }

    const profile_queue & operator += ( const profile_queue & r ) {
	pp_time_add( &sq_await, &r.sq_await );
	pp_time_add( &sq_peek, &r.sq_peek );
	pp_time_add( &qs_peek, &r.qs_peek );
	pp_time_add( &qs_pop, &r.qs_pop );
	pp_time_add( &qv_qhead, &r.qv_qhead );

	num_segment_alloc += r.num_segment_alloc;
	num_segment_dealloc += r.num_segment_dealloc;
	num_tail_wrap += r.num_tail_wrap;

	return *this;
    }

    void dump_profile() const {
#define SHOW(x) pp_time_print( (pp_time_t *)&x, (char *)#x )
	SHOW( sq_await );
	SHOW( sq_peek );
	SHOW( qs_peek );
	SHOW( qs_pop );
	SHOW( qv_qhead );
#undef SHOW
	std::cerr << " num_alloc=" << num_segment_alloc << "\n";
	std::cerr << " num_dealloc=" << num_segment_dealloc << "\n";
	std::cerr << " num_tail_wrap=" << num_tail_wrap << "\n";
    }
};

profile_queue & get_profile_queue();
#endif // PROFILE_QUEUE

template<typename MetaData, typename T>
class write_slice {
    char * buffer;
    size_t length;
    size_t npush;
    queue_version<MetaData> * version;

public:
    write_slice( char * buffer_, size_t length_ )
	: buffer( buffer_ ), length( length_ ), npush( 0 ),
	  version( 0 ) { }
    void set_version( queue_version<MetaData> * version_ ) {
	version = version_;
    }

    size_t get_length() const { return length; }

    void commit() {
	version->push_bookkeeping( npush );
    }

    bool push( T && t ) {
	assert( npush < length );
	*reinterpret_cast<T *>( buffer ) = std::move( t );
	buffer += sizeof(T);
	++npush;
	return npush < length;
    }

    bool push( const T & t ) {
	assert( npush < length );
	*reinterpret_cast<T *>( buffer ) = t;
	buffer += sizeof(T);
	++npush;
	return npush < length;
    }
};


template<typename MetaData, typename T>
class read_slice {
    char * buffer;
    size_t length;
    size_t npop;
    queue_version<MetaData> * version;

public:
    read_slice( char * buffer_, size_t length_ )
	: buffer( buffer_ ), length( length_ ), npop( 0 ), version( 0 ) { }
    void set_version( queue_version<MetaData> * version_ ) {
	version = version_;
    }

    size_t get_length() const { return length; }

    void commit() {
	version->pop_bookkeeping( npop );
    }

    T pop() {
	assert( npop < length );
	char * e = buffer;
	buffer += sizeof(T);
	++npop;
	return std::move( *reinterpret_cast<T *>( e ) );
    }

    const T & peek( size_t off ) const {
	return *reinterpret_cast<const T *>( &buffer[off * sizeof(T)] );
    }
};

class fixed_size_queue
{
    // Cache block 0: owned by producer
    volatile size_t head;
    pad_multiple<CACHE_ALIGNMENT, sizeof(size_t)> pad0;

    // Cache block 1: owned by consumer
    volatile size_t tail;
    pad_multiple<CACHE_ALIGNMENT, sizeof(size_t)> pad1;

    // Cache block 2: unmodified during execution
    const typeinfo_array tinfo;
    const size_t elm_size;
    const size_t size;
    /*const*/ size_t dont_use_mask;
    char * const buffer;
    size_t peekoff;
    pad_multiple<CACHE_ALIGNMENT, sizeof(typeinfo_array) + 4*sizeof(size_t) + sizeof(char *)> pad2;
	
private:
    // Credit:
    // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    static size_t roundup_pow2( size_t v ) {
	v--;
	v |= v>>1;
	v |= v>>2;
	v |= v>>4;
	v |= v>>8;
	v |= v>>16;
	v |= v>>32;
	v++;
	return v;
    }

    // Compute buffer size: max_size elements of size rounded up to 8 bytes
    template<typename T>
    static size_t get_element_size() {
	// return roundup_pow2(((sizeof(T)+7)&~(size_t)7));
	return sizeof(T);
    }

public:
    template<typename T>
    static size_t get_buffer_space( size_t max_size ) {
	// One unused element
	size_t size = (max_size+1) * get_element_size<T>();
	return roundup_pow2( size );
    }

private:
    friend class queue_segment;

    fixed_size_queue( typeinfo_array tinfo_, char * buffer_,
		      size_t elm_size_, size_t max_size, size_t peekoff_ )
	: head( 0 ), tail( peekoff_*elm_size_ ), tinfo( tinfo_ ),
	  elm_size( elm_size_ ),
	  // size( roundup_pow2( (max_size+1) * elm_size ) ), mask( size-1 ),
	  size( (max_size+1) * elm_size ),
	  buffer( buffer_ ), peekoff( peekoff_ ) {
	static_assert( sizeof(fixed_size_queue) % CACHE_ALIGNMENT == 0,
		       "padding failed" );
    }

public:
    ~fixed_size_queue() {
	// Note: the destructor could be made to use sizeof(T) as elm_size
	tinfo.destruct( buffer, &buffer[size], elm_size );
    }

    size_t get_peek_dist() const { return peekoff; }
    void rewind() { tail = 0; } // very first segment has no copied-in peek area
	
    bool empty( size_t off ) const {
	assert( off <= peekoff );
	if( peekoff > 0 ) {
	    // No wrap-around in this situation
	    assert( tail >= head );
	    return head+off*elm_size >= tail;
	} else
	    return head == tail;
    }
    bool full() const volatile {
	// Freeze tail at end of buffer to avoid wrap-around in case of peeking
	size_t full_marker = peekoff > 0 ? 0 : head;
	return ((tail+elm_size) % size) == full_marker;
    }

    bool has_space( size_t length ) const {
	if( head <= tail ) {
	    return ( size - tail - (head == 0 ? 1 : 0) ) >= elm_size*length;
	} else {
	    return ( head - tail - 1 ) >= elm_size*length;
	}
    }
    size_t get_available() const {
	if( head <= tail ) {
	    return ( tail - head ) / elm_size;
	} else {
	    return ( size - head ) / elm_size;
	}
    }
	
    template<typename T>
    const T & peek( size_t pos ) const {
	char* value = &buffer[(head+pos*elm_size) % size];
	return *reinterpret_cast<T *>( value );
    }

    // Rely on return-value optimization to avoid copy constructors
    template<typename T>
    T && pop() {
	assert( elm_size == sizeof(T) );
	char * buffer_pos = &buffer[head];
	// There is always at least one empty element in between tail and head
	head = (head+sizeof(T)) % size;
	return std::move( *reinterpret_cast<T *>( buffer_pos ) );
    }
	
private:
    size_t push_common( size_t elm_size_ ) {
	size_t old_tail = tail;
	assert( !full() );
	tail = (tail+elm_size_) % size;
#if PROFILE_QUEUE
	if( tail < head && old_tail >= head )
	    get_profile_queue().num_tail_wrap++;
#endif
	return old_tail;
    }

public:
    // requires that a place is available by previously checking full()
    template<typename T>
    void push( T && t ) {
	assert( elm_size == sizeof(T) );
	size_t where = push_common( sizeof(T) );
	*reinterpret_cast<T *>( &buffer[where] ) = std::move( t );
    }

    template<typename T>
    void push( const T & t ) {
	assert( elm_size == sizeof(T) );
	size_t where = push_common( sizeof(T) );
	*reinterpret_cast<T *>( &buffer[where] ) = t;
    }

    void pop_bookkeeping( size_t npop ) {
	assert( (head + elm_size * npop) <= size );
	head = (head + elm_size * npop); // % size;
    }

    void push_bookkeeping( size_t npush ) {
	assert( (tail + elm_size * npush) <= size );
	tail = (tail + elm_size * npush) % size;
	assert( tail != head );
    }

    template<typename MetaData, typename T>
    write_slice<MetaData,T> get_write_slice( size_t length ) {
	return write_slice<MetaData,T>( &buffer[tail], length );
    }

    template<typename MetaData, typename T>
    read_slice<MetaData,T> get_read_slice( size_t npop ) {
	return read_slice<MetaData,T>( &buffer[head], npop );
    }

    void copy_peeked( const char * buff ) {
	if( peekoff > 0 )
	    tinfo.copy( buff, &buff[peekoff * elm_size], &buffer[head] );
    }

    const char * get_peek_suffix() const {
	return &buffer[tail - elm_size * peekoff];
    }

    friend std::ostream & operator << ( std::ostream & os, const fixed_size_queue & q );
};

inline std::ostream & operator << ( std::ostream & os, const fixed_size_queue & q ) {
    return os << " QUEUE: head=" << q.head << " tail=" << q.tail
	      << " size=" << q.size << " mask=" << std::hex << 0 // q.mask
	      << std::dec;
}

} //namespace obj

#endif // QUEUE_FIXED_SIZE_QUEUE_H
