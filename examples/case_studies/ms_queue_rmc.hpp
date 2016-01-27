#ifndef RMC_MS_QUEUE_RMC
#define RMC_MS_QUEUE_RMC

#include <rmc++.h>
#include <utility>
#include <experimental/optional>


namespace rmclib {
#if 0
} // f. this
#endif

using std::experimental::optional;

// c.c trolling?
template<class T> using lf_ptr = T*;
const int kCacheLinePadding = 128; // I have NFI


// TODO: memory management; this is written assuming that something
// like GC or epochs will be used to handle freeing memory, but it
// doesn't yet.

// I'm doing this all C++ified, but maybe I shouldn't be.
template<typename T>
class MSQueue {
private:
    struct MSQueueNode {
        rmc::atomic<lf_ptr<MSQueueNode>> next_{nullptr};
        optional<T> data_;

        MSQueueNode() {} // needed for allocating dummy
        MSQueueNode(T &&t) : data_(std::move(t)) {} // is this right
        MSQueueNode(const T &t) : data_(t) {}
    };


    // XXX: alignment
    alignas(kCacheLinePadding)
    rmc::atomic<lf_ptr<MSQueueNode>> head_{nullptr};
    alignas(kCacheLinePadding)
    rmc::atomic<lf_ptr<MSQueueNode>> tail_{nullptr};

    void enqueue_node(lf_ptr<MSQueueNode> node);

public:
    MSQueue() {
        // Need to create a dummy node!
        head_ = tail_ = new MSQueueNode();
    }

    optional<T> dequeue();

    // XXX: allocations!
    void enqueue(T &&t) {
        enqueue_node(new MSQueueNode(std::move(t)));
    }
    void enqueue(const T &t) {
        enqueue_node(new MSQueueNode(t));
    }
};

template<typename T>
void MSQueue<T>::enqueue_node(lf_ptr<MSQueueNode> node) {
    // XXX: start epoch

    // We publish the node in two ways:
    //  * at enqueue, which links it in as the next_ pointer
    //    of the list tail
    //  * at enqueue_swing, which links it in as
    //    the new tail_ of the queue
    // Node initialization needs be be visible before a node
    // publication is.
    VEDGE(node_init, enqueue);
    VEDGE(node_init, enqueue_swing);
    // Make sure to see node init, etc
    // This we should get to use a data-dep on!
    // Pretty sure we don't need XEDGE(get_tail, catchup_swing)...
    XEDGE(get_tail, get_next);
    // Make sure the contents of the next node stay visible
    // to anything that finds it through tail_, when we swing
    VEDGE(get_next, catchup_swing);
    // XXX: how about VEDGE(enqueue, enqueue_swing)??


    // Marker for node initialization. Everything before the
    // enqueue_node call is "init".
    LPRE(node_init);

    lf_ptr<MSQueueNode> tail, next;

    for (;;) {
        tail = L(get_tail, this->tail_);
        next = L(get_next, tail->next_);
        // Check that tail and next are consistent:
        // If we are using an epoch/gc based approach
        // (which we had better be, since we don't have gen counters),
        // this is purely an optimization.
        // XXX: constraint? I think it doesn't matter here, where it is
        // purely an optimization
        if (tail != this->tail_) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            if (L(enqueue, tail->next_.compare_exchange_weak(next, node))) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            L(catchup_swing, this->tail_.compare_exchange_strong(tail, next));
        }
    }

    // Try to swing the tail_ to point to what we inserted
    L(enqueue_swing, this->tail_.compare_exchange_strong(tail, node));

    // XXX: end epoch?
}

template<typename T>
optional<T> MSQueue<T>::dequeue() {
    // XXX: start epoch

    // Core message passing: reading the data out of the node comes
    // after getting the pointer to it.
    XEDGE(get_next, node_use);
    // Make sure we see at least head's init
    XEDGE(get_head, get_next);
    // Need to make sure anything visible through the next pointer
    // stays visible when it gets republished at the head or tail
    VEDGE(get_next, catchup_swing);
    VEDGE(get_next, dequeue);

    lf_ptr<MSQueueNode> head, tail, next;

    for (;;) {
        head = L(get_head, this->head_);
        tail = this->tail_; // XXX: really?
        next = L(get_next, head->next_);

        // Consistency check; see note above
        if (head != this->head_) continue;

        // Check if the queue *might* be empty
        // XXX: is it necessary to have the empty check under this
        if (head == tail) {
            // Ok, so, the queue might be empty, but it also might
            // be that the tail pointer has just fallen behind.
            // If the next pointer is null, then it is actually empty
            if (next == nullptr) {
                return optional<T>{};
            } else {
                // not empty: tail falling behind; since it is super
                // not ok for the head to advance past the tail,
                // try advancing the tail
                // XXX weak v strong?
                L(catchup_swing,
                  this->tail_.compare_exchange_strong(tail, next));
            }
        } else {
            // OK, now we try to actually read the thing out.

            // If we weren't planning to rely on epochs or something,
            // note that we would need to read out the data *before* we
            // do the CAS, or else things are gonna get bad.
            // (could get reused first)
            if (L(dequeue, this->head_.compare_exchange_weak(head, next))) {
                break;
            }
        }
    }

    LPOST(node_use);

    // OK, everything set up.
    // next contains the value we are reading
    // head can be freed
    //epoch_free(head); // XXX or something
    optional<T> ret(std::move(next->data_));
    next->data_ = optional<T>{}; // destroy the object

    // XXX: end epoch?

    return ret;
}

}


#endif
