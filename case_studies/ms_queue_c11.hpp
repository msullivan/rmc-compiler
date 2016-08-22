// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_C11
#define RMC_MS_QUEUE_C11

#include <atomic>
#include <utility>
#include "util.hpp"
#include "epoch.hpp"

namespace rmclib {

// I'm doing this all C++ified, but maybe I shouldn't be.
template<typename T>
class MSQueue {
private:
    struct MSQueueNode {
        std::atomic<lf_ptr<MSQueueNode>> next_{nullptr};
        optional<T> data_;

        MSQueueNode() {} // needed for allocating dummy
        MSQueueNode(T &&t) : data_(std::move(t)) {} // is this right
        MSQueueNode(const T &t) : data_(t) {}
    };


    alignas(kCacheLinePadding)
    std::atomic<lf_ptr<MSQueueNode>> head_{nullptr};
    alignas(kCacheLinePadding)
    std::atomic<lf_ptr<MSQueueNode>> tail_{nullptr};

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
        // acquire because we need to see node init
        tail = this->tail_.load(mo_acq);
        // acquire because anything we see through this needs to be
        // re-published if we try to do a catchup swing: **
        next = tail->next_.load(mo_acq);
        // Check that tail and next are consistent:
        // If we are using an epoch/gc based approach
        // (which we had better be, since we don't have gen counters),
        // this is purely an optimization.
        // XXX: order? I think it doesn't matter since in this version
        // this check is an optimization??
        if (tail != this->tail_.load(mo_rlx)) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            // release because publishing; not acquire since I don't
            // think we care what we see
            if (tail->next_.compare_exchange_weak(next, node,
                                                  mo_rel, mo_rlx)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            // release because we need to keep the node data visible
            // (**) - maybe can put an acq_rel *fence* here instead
            this->tail_.compare_exchange_strong(tail, next,
                                                mo_rel, mo_rlx);
        }
    }

    // Try to swing the tail_ to point to what we inserted
    // release because publishing
    this->tail_.compare_exchange_strong(tail, node, mo_rel, mo_rlx);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    auto guard = Epoch::pin();

    lf_ptr<MSQueueNode> head, tail, next;

    for (;;) {
        head = this->head_.load(mo_acq);
        tail = this->tail_.load(mo_acq);
        // This one could maybe use an acq/rel fence
        next = head->next_.load(mo_acq);

        // Consistency check; see note above
        // no ordering because just an optimization thing
        if (head != this->head_.load(mo_rlx)) continue;

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
                // Release because anything we saw needs to be republished
                this->tail_.compare_exchange_strong(tail, next,
                                                    mo_rel, mo_rlx);
            }
        } else {
            // OK, now we try to actually read the thing out.

            // If we weren't planning to rely on epochs or something,
            // note that we would need to read out the data *before* we
            // do the CAS, or else things are gonna get bad.
            // (could get reused first)

            // release because we're republishing; don't care about what we read
            if (this->head_.compare_exchange_weak(head, next,
                                                  mo_rel, mo_rlx)) {
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
