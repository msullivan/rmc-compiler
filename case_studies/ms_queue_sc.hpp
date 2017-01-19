// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_SC
#define RMC_MS_QUEUE_SC

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
        tail = this->tail_;
        next = tail->next_;
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

    // Try to swing the tail_ to point to what we inserted
    this->tail_.compare_exchange_strong(tail, node);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    auto guard = Epoch::pin();

    lf_ptr<MSQueueNode> head, next;

    for (;;) {
        head = this->head_;
        next = head->next_;

        // Consistency check; see note above
        if (head != this->head_) continue;

        // Is the queue empty?
        if (next == nullptr) {
            return optional<T>{};
        } else {
            // OK, now we try to actually read the thing out.
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
