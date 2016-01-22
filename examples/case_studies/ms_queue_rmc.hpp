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
        T data_;

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

    for (;;) {
        lf_ptr<MSQueueNode> tail = this->tail_;
        lf_ptr<MSQueueNode> next = tail->next_;
        // Check that tail and next are consistent:
        // If we are using an epoch/gc based approach
        // (which we had better be, since we don't have gen counters),
        // this is purely an optimization.
        if (tail != this->tail_) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            if (tail->next_.compare_exchange_weak(next, node)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            this->tail_.compare_exchange_strong(tail, next);
        }
    }

    // XXX: end epoch?
}

template<typename T>
optional<T> MSQueue<T>::dequeue() {
    lf_ptr<MSQueueNode> head, tail, next;

    for (;;) {
        head = this->head_;
        tail = this->tail_;
        next = head->next_;

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
                this->tail_.compare_exchange_strong(tail, next);
            }
        } else {
            // OK, now we try to actually read the thing out.

            // If we weren't planning to rely on epochs or something,
            // note that we would need to read out the data *before* we
            // do the CAS, or else things are gonna get bad.
            // (could get reused first)
            if (this->head_.compare_exchange_weak(head, next)) {
                break;
            }
        }
    }

    // OK, everything set up.
    // next contains the value we are reading
    // head can be freed
    //epoch_free(head); // XXX or something
    optional<T> ret(std::move(next->data_));
    next->data_.~T(); // call destructor

    return ret;
}

}


#endif
