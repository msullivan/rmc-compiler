// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_C112
#define RMC_MS_QUEUE_C112

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
    std::atomic<gen_ptr<MSQueueNode *>> next_;
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
    using NodePtr = gen_ptr<MSQueueNode *>;

    alignas(kCacheLinePadding)
    std::atomic<NodePtr> head_;
    alignas(kCacheLinePadding)
    std::atomic<NodePtr> tail_;

    void enqueueNode(MSQueueNode *node);

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
void MSQueue<T>::enqueueNode(MSQueueNode *node) {
    node->next_ = node->next_.load().update(nullptr); // XXX: ok?

    NodePtr tail, next;

    for (;;) {
        // acquire because we need to see node init
        tail = this->tail_.load(mo_acq);
        // acquire because anything we see through this needs to be
        // re-published if we try to do a catchup swing: **
        next = tail->next_.load(mo_acq);
        // Check that tail and next are consistent: In this gen
        // counter version, this is important for correctness: if,
        // after we read the tail, it gets removed from this queue,
        // freed, and added to some other queue, we need to make sure
        // that we don't try to append to that queue instead.
        // XXX: So I think that means it should be acquire
        if (tail != this->tail_.load(mo_acq)) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            // release because publishing; not acquire since I don't
            // think we care what we see
            if (tail->next_.compare_exchange_weak(next, next.inc(node),
                                                  mo_rel, mo_rlx)) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            // release because we need to keep the node data visible
            // (**) - maybe can put an acq_rel *fence* here instead
            this->tail_.compare_exchange_strong(tail, tail.inc(next),
                                                mo_rel, mo_rlx);
        }
    }

    // Try to swing the tail_ to point to what we inserted
    // release because publishing
    this->tail_.compare_exchange_strong(tail, tail.inc(node), mo_rel, mo_rlx);
}

template<typename T>
rmc_noinline
optional<T> MSQueue<T>::dequeue() {
    NodePtr head, tail, next;

    universal data;

    for (;;) {
        head = this->head_.load(mo_acq);
        tail = this->tail_.load(mo_acq);
        // This one could maybe use an acq/rel fence
        next = head->next_.load(mo_acq);

        // Consistency check; see note above
        if (head != this->head_.load(mo_acq)) continue;

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
                this->tail_.compare_exchange_strong(tail, tail.inc(next),
                                                    mo_rel, mo_rlx);
            }
        } else {
            // OK, now we try to actually read the thing out.

            // We need to read the data out of the node
            // *before* we try to dequeue it or else it could get
            // reused before we read it out.
            data = next->data_;

            // release because we're republishing; don't care about what we read
            if (this->head_.compare_exchange_weak(head, head.inc(next),
                                                  mo_rel, mo_rlx)) {
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
