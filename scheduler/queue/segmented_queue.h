// -*- c++ -*-

#ifndef QUEUE_SEGMENTED_QUEUE_H
#define QUEUE_SEGMENTED_QUEUE_H

#include <iostream>
#include "swan/queue/queue_segment.h"

namespace obj {

class queue_index;

class segmented_queue_base {
protected:
    // Place-holder until we have allocated a queue_segment to hold
    // this value.
    long logical;

public:
    segmented_queue_base() : logical( -1 ) { }

    long get_logical() const { return logical; }
    void set_logical( long logical_ ) { logical = logical_; }
};

class segmented_queue;
class segmented_queue_headonly;
class segmented_queue_pop;
class segmented_queue_push;

inline std::ostream &
operator << ( std::ostream & os, const segmented_queue & seg );
inline std::ostream &
operator << ( std::ostream & os, const segmented_queue_push & seg );
inline std::ostream &
operator << ( std::ostream & os, const segmented_queue_pop & seg );


// TODO: create base class with reduce functionality and subclasses
// with either push or pop functionality. We will never push and pop from
// the same segmented_queue. Rather, a pushpopdep procedure will push to user
// and pop from queue (in queue_version). These subclasses are our views on
// the queue and may limit the number of pushes and pops performed.
// Note: pop functionality applies to a segmented_queue where only the head
// is known. Push may operate on segmented_queues with a head and a tail.
// Note that there is a slight asymmetry between push and pop as concurrent
// pushes to the same hyperqueue will push to different segments by
// construction. Pops may occur concurrently on the same segments, as may
// pop and push.
class segmented_queue : public segmented_queue_base {
protected:
    queue_segment * head, * tail;

    // head may be NULL and tail non-NULL. In this case, the list has been
    // reduced but we know that the current segmented_queue is the last such
    // one (the right-most in the reduction) and therefore any addition
    // in the queue goes to the tail and growing the segment grows the tail.
    // if head is NULL, the segments should not be deallocated?
    // NOTE: there may be multiple links to a tail segment. How do we update
    // them?

public: 
    segmented_queue() : head( 0 ), tail( 0 ) { }
    void erase( queue_index & idx ) {
	// Ownership is determined when both head and tail are non-NULL.
	/*
	errs() << "destruct qseg: head=" << head << " tail=" << tail << "\n";
	if( head ) errs() << "         head: " << *head << "\n"; 
	if( tail ) errs() << "         tail: " << *tail << "\n"; 
	*/

	if( head != 0 && tail != 0 ) {
	    for( queue_segment * q=head, * q_next; q != tail; q = q_next ) {
		q_next = q->get_next();
		if( q->get_slot() >= 0 ) {
		    idx.erase( q->get_slot() );
		    // errs() << "ERROR: cannot free local segment if slotted\n";
		    // abort();
		}
		delete q;
	    }
	}
    }

    queue_segment * get_tail() { return tail; }
    queue_segment * get_head() { return head; }
    const queue_segment * get_tail() const { return tail; }
    const queue_segment * get_head() const { return head; }
    void set_head( queue_segment * seg ) { head = seg; }

    void reset() {
	head = tail = 0;
	logical = -1;
    }

    void take( segmented_queue & from ) {
	std::swap( *this, from );
    }

    // Retain from.logical as we are keeping the from queue segment at the
    // same logical position!
    segmented_queue split() {
	segmented_queue h;
	h.head = head;
	h.logical = logical;
	head = 0;
	return h;
    }

public:
    segmented_queue &
    reduce( segmented_queue & right, queue_index & idx ) {
	if( !tail ) {
	    if( !head ) {
		head = right.get_head();
		logical = right.get_logical();
	    }
	    tail = right.get_tail();
	    right.reset();
	} else if( right.get_head() ) {
	    if( tail->get_logical_tail() >= 0 )
		right.get_head()->propagate_logical_pos(
		    tail->get_logical_tail(), idx );
	    tail->set_next( right.get_head() );
	    tail = right.get_tail(); // may be 0
	    right.reset();
	} else {
	    assert( !right.get_tail() );
	}
	return *this;
    }

    segmented_queue &
    reduce_reverse( segmented_queue & left, queue_index & idx ) {
	left.reduce( *this, idx );
	std::swap( left, *this );
	return *this;
    }

    segmented_queue &
    reduce_headonly( segmented_queue & right, queue_index & idx ) {
	assert( right.get_head() && !right.get_tail() );

	if( !tail ) {
	    if( !head ) {
		head = right.get_head();
		logical = right.get_logical();
	    }
	    right.reset();
	} else {
	    if( tail->get_logical_tail() >= 0 )
		right.get_head()->propagate_logical_pos( 
		    tail->get_logical_tail(), idx );
	    tail->set_next( right.get_head() );
	    tail = 0;
	    right.reset();
	}
	return *this;
    }
};

class segmented_queue_headonly : public segmented_queue_base {
protected:
    queue_segment * head;

public: 
    segmented_queue_headonly() : head( 0 ) { }

    queue_segment * get_tail() { return 0; }
    queue_segment * get_head() { return head; }
    const queue_segment * get_tail() const { return 0; }
    const queue_segment * get_head() const { return head; }
    void set_head( queue_segment * seg ) { head = seg; }

    void reset() {
	head = 0;
	logical = -1;
    }

    void take( segmented_queue_headonly & from ) {
	std::swap( *this, from );
    }
};

class segmented_queue_push : public segmented_queue {
    size_t volume_push;

public: 
    segmented_queue_push() : volume_push( 0 ) { }

    // For printing (operator <<)
    size_t get_volume_push() const { return volume_push; }

    template<typename T>
    void push_segment( long logical_pos, size_t max_size, queue_index & idx ) {
	logical = logical_pos; // tail ? tail->get_logical_tail() : -1;
	queue_segment * seg = queue_segment::template create<T>( logical, max_size );
	// seg->set_producing();
	if( tail ) {
	    tail->set_next( seg );
	    tail->clr_producing();
	} else {
	    assert( !head && "if tail == 0, then also head == 0" );
	    head = seg;
	}
	tail = seg;

	if( logical >= 0 )
	    idx.insert( tail );
    }

    template<typename T>
    void push( T * value, size_t max_size, queue_index & idx ) {
	assert( tail );
	// TODO: could introduce a delay here, e.g. if concurrent pop exists,
	// then just wait a bit for the pop to catch up and avoid inserting
	// a new segment.
	if( tail->is_full() )
	    push_segment<T>( tail->get_logical_tail(), max_size, idx );
	errs() << "push on queue segment " << *tail << " SQ=" << *this << "\n";
	tail->push( value );
	volume_push++;
    }
};

class segmented_queue_pop : public segmented_queue_headonly {
    size_t volume_pop;

public: 
    segmented_queue_pop() : volume_pop( 0 ) { }

    size_t get_volume_pop() const { return volume_pop; }

private:
    void await( queue_index & idx ) {
	assert( head );

	// As long as nothing has appeared in the queue and the producing
	// flag is on, we don't really know if the queue is empty or not.
	// Spin until something appears at the next index we will read.
	size_t pos = get_index();
	do {
	    while( head->is_empty( pos ) && head->is_producing() ) {
		// busy wait
		sched_yield();
	    }
	    if( !head->is_empty( pos ) )
		return;
	    if( !head->is_producing() ) {
		if( queue_segment * seg = head->get_next() ) {
		    // Note: this case is executed only once per segment,
		    // namely for the task that pops the tail of this segment.

		    // errs() << "head " << head << " runs out, pop segment (empty)\n";
		    // Are we totally done with this segment?
		    // TODO: this may introduce a data race and not deallocate
		    // some segments as a result. Or even dealloc more than
		    // once...
		    bool erase
			= head->get_volume_pop() == head->get_volume_push();

		    // Every segment linked from a known position (we can
		    // only pop from a known position) should also be known.
		    // However, there is a race condition in the reduction
		    // and updating of the logicial position that may occur
		    // (rarely) and that may imply that the value has not been
		    // properly propagated yet. We fix the correctness aspect
		    // of that here, propagating the position if needed.
		    // There is also a potential performance aspect as in
		    // case of those races we will not be able to find some
		    // segments in the index during some time.
		    if( seg->get_logical_pos() < 0 )
			seg->propagate_logical_pos( head->get_logical_tail(), idx );

		    // Compute our new position based on logical_pos, which is
		    // constant (as opposed to head and tail which differ by
		    // the number of pushes and pops performed).
		    logical = seg->get_logical_pos();
		    volume_pop = pos - logical;

		    // TODO: when to delete a segment in case of multiple
		    // consumers?
		    if( erase ) {
			if( erase && head->get_slot() >= 0 ) {
			    idx.erase( head->get_slot() );
			    head->set_slot( -1 );
			}
			delete head;
		    }

		    head = seg;
		} else {
		    // In this case, we know the queue is empty.
		    // This may be an error or not, depending on whether
		    // we are polling the queue for emptiness, or trying
		    // to pop.
		    return;
		}
	    }
	} while( true );
    }

public:
    bool empty( queue_index & idx ) {
	assert( head );

	// Is there anything in the queue? If so, return not empty
	if( likely( !head->is_empty( get_index() ) ) )
	    return false;

	// Spin until we are sure about emptiness (nothing more to be produced
	// for sure).
	await( idx );

	// Now check again. This result is for sure.
	return head->is_empty( get_index() );
    }

    size_t get_index() const {
	return logical + volume_pop;
    }

    template<typename T>
    void pop( T & t, queue_index & idx ) {
	// Spin until the desired information appears in the queue.
	await( idx );

	// We must be able to pop now.
	size_t pos = get_index();
	if( !head->is_empty( pos ) ) {
	    head->pop( t, pos );
	    volume_pop++;
	    // errs() << "pop from queue " << head << ": value="
		   // << std::dec << t << ' ' << *head
		   // << " SQ=" << *this
		   // << " position=" << pos
		   // << "\n";
	    return;
	}

	assert( !head->is_producing() );
	abort();
    }
};

std::ostream &
operator << ( std::ostream & os, const segmented_queue & seg ) {
    return os << '{' << seg.get_head()
	      << ", " << seg.get_tail()
	      << ", @" << seg.get_logical()
	      << '}';
}

std::ostream &
operator << ( std::ostream & os, const segmented_queue_push & seg ) {
    return os << '{' << seg.get_head()
	      << ", " << seg.get_tail()
	      << ", @" << seg.get_logical()
	      << "+" << seg.get_volume_push()
	      << '}';
}

std::ostream &
operator << ( std::ostream & os, const segmented_queue_pop & seg ) {
    return os << '{' << seg.get_head()
	      << ", " << seg.get_tail()
	      << ", @" << seg.get_logical()
	      << "-" << seg.get_volume_pop()
	      << '}';
}

}//namespace obj

#endif // QUEUE_SEGMENTED_QUEUE_H
