// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_BARRIER
#define RMC_MS_QUEUE_BARRIER

#include <atomic.h>
#include <rmc++.h>
#include <utility>
#include "util.hpp"
#include "epoch.hpp"

namespace rmclib {

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

    void enqueue(T &&t) {
        enqueue_node(new MSQueueNode(std::move(t)));
    }
    void enqueue(const T &t) {
        enqueue_node(new MSQueueNode(t));
    }
};

template<typename T>
rmc_noinline
void MSQueue<T>::enqueue_node(lf_ptr<MSQueueNode> node) {
    auto guard = Epoch::pin();

    lf_ptr<MSQueueNode> tail, next;

    for (;;) {
        tail = this->tail_;
        // N.B. NO BARRIER
        next = tail->next_;
        // RMC put a barrier here
        // XXX: I don't understand why it isn't sunk down more!
        smp_mb();

        // Check that tail and next are consistent:
        // If we are using an epoch/gc based approach
        // (which we had better be, since we don't have gen counters),
        // this is purely an optimization.
        if (tail != this->tail_) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            smp_mb(); //asdf
            if (tail->next_.compare_exchange_weak(next, node)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            smp_mb(); //asdf
            this->tail_.compare_exchange_strong(tail, next);
        }
    }
    // This is here because of the enqueue->enqueue_swing edge
    smp_mb();
    // Try to swing the tail_ to point to what we inserted
    this->tail_.compare_exchange_strong(tail, node);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    auto guard = Epoch::pin();

    lf_ptr<MSQueueNode> head, tail, next;

    for (;;) {
        head = this->head_;
        smp_mb();
        tail = this->tail_;
        smp_mb();
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
                smp_mb();
                this->tail_.compare_exchange_strong(tail, next);
            }
        } else {
            // OK, now we try to actually read the thing out.

            // If we weren't planning to rely on epochs or something,
            // note that we would need to read out the data *before* we
            // do the CAS, or else things are gonna get bad.
            // (could get reused first)
            smp_mb();
            if (this->head_.compare_exchange_weak(head, next)) {
                break;
            }
        }
    }

    // OK, everything set up.
    // next contains the value we are reading
    // head can be freed
    guard.unlinked(head);
    optional<T> ret(std::move(next->data_));
    next->data_ = optional<T>{}; // destroy the object

    return ret;
}

}


#endif
