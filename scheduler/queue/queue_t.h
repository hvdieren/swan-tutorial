// -*- c++ -*-

#ifndef QUEUE_QUEUE_T_H
#define QUEUE_QUEUE_T_H

#include "swan/queue/queue_version.h"

namespace obj {

// Base classes for dependency tags

template<typename QMetaData>
class queuedep_tags_base : public all_tags_base {
    queue_version<QMetaData> queue;

public:
    queuedep_tags_base( queue_version<QMetaData> * parent, bool is_push )
	: queue( parent, is_push ) { }

    queue_version<QMetaData> * get_queue_version() { return &queue; }
};


template<typename QMetaData>
class popdep_tags_base : public queuedep_tags_base<QMetaData> {
public:
    popdep_tags_base( queue_version<QMetaData> * parent )
	: queuedep_tags_base<QMetaData>( parent, false ) { }
};

template<typename QMetaData>
class pushpopdep_tags_base : public queuedep_tags_base<QMetaData> {
public:
    pushpopdep_tags_base( queue_version<QMetaData> * parent )
	: queuedep_tags_base<QMetaData>( parent, true ) { }
};

template<typename QMetaData>
class pushdep_tags_base : public queuedep_tags_base<QMetaData> {
public:
    pushdep_tags_base( queue_version<QMetaData> * parent )
	: queuedep_tags_base<QMetaData>( parent, true ) { }
};

template<typename QMetaData>
class prefixdep_tags_base : public queuedep_tags_base<QMetaData> {
public:
    prefixdep_tags_base( queue_version<QMetaData> * parent )
	: queuedep_tags_base<QMetaData>( parent, false ) { }
};




// For taskgraph specific scheme

// TODO: generalize this.
typedef tkt_metadata queue_metadata;

// Popdep (input) dependency tags - fully serialized with other pop and pushpop
class popdep_tags : public popdep_tags_base<queue_metadata> {
    template<typename MetaData, typename Task, template<typename T> class DepTy>
    friend class dep_traits;
    tkt_metadata::tag_t rd_tag;

public:
    popdep_tags( queue_version<queue_metadata> * parent )
	: popdep_tags_base( parent ) { }
};

// Pushpopdep (input/output) dependency tags - fully serialized with other
// pop and pushpop
class pushpopdep_tags : public pushpopdep_tags_base<queue_metadata> {
    template<typename MetaData, typename Task, template<typename T> class DepTy>
    friend class dep_traits;
    tkt_metadata::tag_t rd_tag;

public:
    pushpopdep_tags( queue_version<queue_metadata> * parent )
	: pushpopdep_tags_base( parent ) { }
};

// Pushdep (output) dependency tags
class pushdep_tags : public pushdep_tags_base<queue_metadata>,
		     public serial_dep_tags {
    template<typename MetaData, typename Task, template<typename T> class DepTy>
    friend class dep_traits;

public:
    pushdep_tags( queue_version<queue_metadata> * parent )
	: pushdep_tags_base( parent ) { }
};

// Prefixdep (output) dependency tags
class prefixdep_tags : public prefixdep_tags_base<queue_metadata>,
		       public serial_dep_tags {
    template<typename MetaData, typename Task, template<typename T> class DepTy>
    friend class dep_traits;
    tkt_metadata::tag_t rd_tag;

public:
    prefixdep_tags( queue_version<queue_metadata> * parent )
	: prefixdep_tags_base( parent ) { }
};

// queue_base: an instance of a queue, base class for queue_t, pushdep, popdep,
// pushpopdep, prefixdep.
// This class may not have non-trival constructors nor destructors in order to
// reap the simplified x86-64 calling conventions for small structures (the
// only case we support), in particular for passing pushdep and popdep
// as direct arguments. We don't support this for queue_t.
template<typename MetaData>
class queue_base
{
public:
    typedef MetaData metadata_t;

    template<typename T>
    friend class queue_t;

protected:
    queue_version<metadata_t> * queue_v;
	
public:
    const queue_version<metadata_t> * get_version() const { return queue_v; }
    queue_version<metadata_t> * get_version() { return queue_v; }
	
    void set_version( queue_version<metadata_t> * v ) { queue_v = v; }

protected:
    queue_version<metadata_t> * get_nc_version() const { return queue_v; }

    template<typename DepTy>
    DepTy create_dep_ty() const {
	DepTy od;
	od.queue_v = this->get_nc_version();
	return od;
    }
};

// queue_t: programmer's instance of a queue

template<typename T>
class queue_t : protected queue_version<queue_metadata>
{
public:
    queue_t() : queue_version<queue_metadata>( q_typeinfo::create<T>() ) {
    }
	
    operator pushdep<T>() const { return create_dep_ty< pushdep<T> >(); }
    operator popdep<T>()  const { return create_dep_ty< popdep<T> >(); }
    operator pushpopdep<T>()  const { return create_dep_ty< pushpopdep<T> >(); }
    operator prefixdep<T>()  const { return create_dep_ty< prefixdep<T> >(); }
	
    // The queue_t works in push/pop mode and so supports empty, pop and push.
    bool empty() { return queue_version<queue_metadata>::empty(); }

    T pop() {
	T t;
	queue_version<queue_metadata>::pop( t );
	return t;
    }

    void push( T & t ) {
	queue_version<queue_metadata>::push( t );
    }
	
private:
    template<typename DepTy>
    DepTy create_dep_ty() const {
	DepTy od;
	od.queue_v = get_nc_version();
	return od;
    }

protected:
    queue_version<metadata_t> * get_nc_version() const {
	return const_cast<queue_version<queue_metadata>*>(
	    static_cast<const queue_version<queue_metadata>*>( this ) );
    }
	
public:
    // For concepts: need not be implemented, must be non-static and public
    void is_object_decl(void);
};


template<typename T>
class pushdep : public queue_base<queue_metadata>
{
public:
    typedef queue_metadata metadata_t;
    typedef pushdep_tags dep_tags;
    typedef pushdep_type_tag _object_tag;
	
	// For concepts: need not be implemented, must be non-static and public
    void is_object_decl(void);

    static pushdep<T> create( queue_version<metadata_t> * v ) {
	pushdep<T> dep;
	dep.queue_v = v;
	return dep;
    }
    
    // void push(T & value) { queue_v->push( value ); }
    void push(T value) { queue_v->push( value ); }
};

template<typename T>
class popdep : public queue_base<queue_metadata>
{
public:
    typedef queue_metadata metadata_t;
    typedef popdep_tags dep_tags;
    typedef popdep_type_tag _object_tag;
	
	
    static popdep<T> create( queue_version<metadata_t> * v ) {
	popdep<T> newpop;
	newpop.queue_v = v;
	return newpop;
    }
	
    T pop() {
	T t;
	queue_v->pop( t );
	return t;
    }
	
    bool empty() { return queue_v->empty(); }

public:
    // For concepts: need not be implemented, must be non-static and public
    void is_object_decl(void);
};

template<typename T>
class pushpopdep;

template<typename T>
class prefixdep;

} //end namespace obj

#ifdef __x86_64__
#include "swan/platform_x86_64.h"

namespace platform_x86_64 {

// Specialization - declare to the implementation of the calling convention
// that obj::indep a.o. are a struct with two members. The implementation
// of the calling convention will then figure out whether this struct is
// passed in registers or not. (If it is not - you should not pass it and
// a compile-time error will be triggered). This approach is a low-cost
// way to by-pass the lack of data member introspection in C++.
template<size_t ireg, size_t freg, size_t loff, typename T>
struct arg_passing<ireg, freg, loff, obj::queue_t<T> >
    : arg_passing_struct1<ireg, freg, loff, obj::queue_t<T>, obj::queue_version<typename obj::queue_t<T>::metadata_t> *> {
};

template<size_t ireg, size_t freg, size_t loff, typename T>
struct arg_passing<ireg, freg, loff, obj::popdep<T> >
    : arg_passing_struct1<ireg, freg, loff, obj::popdep<T>, obj::queue_version<typename obj::popdep<T>::metadata_t> *> {
};

template<size_t ireg, size_t freg, size_t loff, typename T>
struct arg_passing<ireg, freg, loff, obj::pushdep<T> >
    : arg_passing_struct1<ireg, freg, loff, obj::pushdep<T>, obj::queue_version<typename obj::pushdep<T>::metadata_t> *> {
};

} // namespace platform_x86_64
#endif

#endif // QUEUE_QUEUE_T_H
