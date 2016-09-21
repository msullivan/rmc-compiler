// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_SC2
#define RMC_MS_QUEUE_SC2

#include <atomic>
#include <utility>
#include "util.hpp"
#include "gen_ptr.hpp"
#include "freelist.hpp"
#include "universal.hpp"

// A version of Michael-Scott queues that closely corresponds to the
// original version using generation pointers.

namespace rmclib {

struct MSQueueNode {
    std::gen_atomic<lf_ptr<MSQueueNode>> next_;
    // Traditional MS Queues need to read out the data from nodes
    // that may already be getting reused.
    // To avoid data races copying the elements around,
    // we instead store a pointer to an allocated object.
    universal data_;

    static Freelist<MSQueueNode> freelist;
};

template<typename T>
class MSQueue {
private:
    using NodePtr = gen_ptr<lf_ptr<MSQueueNode>>;

    alignas(kCacheLinePadding)
    std::gen_atomic<lf_ptr<MSQueueNode>> head_;
    alignas(kCacheLinePadding)
    std::gen_atomic<lf_ptr<MSQueueNode>> tail_;

    void enqueueNode(lf_ptr<MSQueueNode> node);

public:
    MSQueue() {
        // Need to create a dummy node!
        auto node = MSQueueNode::freelist.alloc();
        node->next_ = node->next_.load().update(nullptr); // XXX: ok?
        head_ = tail_ = NodePtr(node, 0);
    }

    optional<T> dequeue();

    void enqueue(T &&t) {
        auto node = MSQueueNode::freelist.alloc();
        node->data_ = universal(std::move(t));
        enqueueNode(node);
    }
    void enqueue(const T &t) {
        auto node = MSQueueNode::freelist.alloc();
        node->data_ = universal(t);
        enqueueNode(node);
    }
};

template<typename T>
rmc_noinline
void MSQueue<T>::enqueueNode(lf_ptr<MSQueueNode> node) {
    node->next_ = node->next_.load().update(nullptr); // XXX: ok?

    NodePtr tail, next;

    for (;;) {
        tail = this->tail_;
        next = tail->next_;
        // Check that tail and next are consistent: In this gen
        // counter version, this is important for correctness: if,
        // after we read the tail, it gets removed from this queue,
        // freed, and added to some other queue, we need to make sure
        // that we don't try to append to that queue instead.
        if (tail != this->tail_) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            if (tail->next_.compare_exchange_weak_gen(next, node)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            this->tail_.compare_exchange_strong_gen(tail, next);
        }
    }

    // Try to swing the tail_ to point to what we inserted
    this->tail_.compare_exchange_strong_gen(tail, node);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    NodePtr head, tail, next;

    universal data;

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
                this->tail_.compare_exchange_strong_gen(tail, next);
            }
        } else {
            // OK, now we try to actually read the thing out.

            // We need to read the data out of the node
            // *before* we try to dequeue it or else it could get
            // reused before we read it out.
            data = next->data_;
            if (this->head_.compare_exchange_weak_gen(head, next)) {
                break;
            }
        }
    }

    // OK, everything set up.
    // head can be freed
    MSQueueNode::freelist.unlinked(head);
    optional<T> ret(data.extract<T>());

    return ret;
}

}


#endif
