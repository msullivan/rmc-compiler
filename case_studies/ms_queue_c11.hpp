// Copyright (c) 2014-2017 Michael J. Sullivan
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
        std::atomic<MSQueueNode *> next_{nullptr};
        optional<T> data_;

        MSQueueNode() {} // needed for allocating dummy
        MSQueueNode(T &&t) : data_(std::move(t)) {} // is this right
        MSQueueNode(const T &t) : data_(t) {}
    };


    alignas(kCacheLinePadding)
    std::atomic<MSQueueNode *> head_{nullptr};
    alignas(kCacheLinePadding)
    std::atomic<MSQueueNode *> tail_{nullptr};

    void enqueue_node(MSQueueNode *node);

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

/// BEGIN SNIP
template<typename T>
rmc_noinline
void MSQueue<T>::enqueue_node(MSQueueNode *node) {
    auto guard = Epoch::pin();

    MSQueueNode *tail, *next;

    for (;;) {
        // acquire because we need to see node init
        tail = this->tail_.load(std::memory_order_acquire);
        // acquire because anything we see through this needs to be
        // re-published if we try to do a catchup swing
        next = tail->next_.load(std::memory_order_acquire);

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in.
            // release because publishing; not acquire since we don't
            // care what we see
            if (tail->next_.compare_exchange_weak(
                    next, node,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            // release because we need to keep the node data visible
            this->tail_.compare_exchange_strong(
                tail, next,
                std::memory_order_release,
                std::memory_order_relaxed);
        }
    }

    // Try to swing the tail_ to point to what we inserted
    // release because publishing
    this->tail_.compare_exchange_strong(
        tail, node,
        std::memory_order_release,
        std::memory_order_relaxed);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    auto guard = Epoch::pin();

    MSQueueNode *head, *next;

    for (;;) {
        head = this->head_.load(std::memory_order_acquire);
        next = head->next_.load(std::memory_order_acquire);

        // Is the queue empty?
        if (next == nullptr) {
            return optional<T>{};
        } else {
            // OK, actually pop the head off now.
            // release because we're republishing; don't care what we read
            if (this->head_.compare_exchange_weak(
                    head, next,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
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
/// END SNIP

}


#endif
