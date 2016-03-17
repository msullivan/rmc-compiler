#ifndef RMC_MS_QUEUE_SC
#define RMC_MS_QUEUE_SC

#include <atomic>
#include <utility>
#include "util.hpp"
#include "gen_ptr.hpp"
#include "freelist.hpp"

// A version of Michael-Scott queues that closely corresponds to the
// original version using generation pointers.

namespace rmclib {

template<typename T>
class MSQueue {
private:
    struct MSQueueNode;
    using NodePtr = gen_ptr<lf_ptr<MSQueueNode>>;
    struct MSQueueNode {
        std::atomic<NodePtr> next_;
        optional<T> data_;
    };


    alignas(kCacheLinePadding)
    std::atomic<NodePtr> head_;
    alignas(kCacheLinePadding)
    std::atomic<NodePtr> tail_;

    Freelist<MSQueueNode> freelist_; // XXX: should be global :( :( :(

    void enqueueNode(lf_ptr<MSQueueNode> node);

public:
    MSQueue() {
        // Need to create a dummy node!
        auto node = freelist_.alloc();
        node->next_ = node->next_.load().update(nullptr); // XXX: ok?
        head_ = tail_ = NodePtr(node, 0);
    }

    optional<T> dequeue();

    void enqueue(T &&t) {
        auto node = freelist_.alloc();
        node->data_ = std::move(t);
        enqueueNode(node);
    }
    void enqueue(const T &t) {
        auto node = freelist_.alloc();
        node->data_ = t;
        enqueueNode(node);
    }
};

template<typename T>
void MSQueue<T>::enqueueNode(lf_ptr<MSQueueNode> node) {
    node->next_ = node->next_.load().update(nullptr); // XXX: ok?

    NodePtr tail, next;

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
            if (tail->next_.compare_exchange_weak(next, next.update(node))) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            this->tail_.compare_exchange_strong(tail, tail.update(next));
        }
    }

    // Try to swing the tail_ to point to what we inserted
    this->tail_.compare_exchange_strong(tail, tail.update(node));
}

template<typename T>
optional<T> MSQueue<T>::dequeue() {
    NodePtr head, tail, next;

    optional<T> ret;

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
                this->tail_.compare_exchange_strong(tail, tail.update(next));
            }
        } else {
            // OK, now we try to actually read the thing out.

            // Unfortunately we need to read the data out of the node
            // *before* we try to dequeue it or else it could get
            // reused before we read it out.
            // This unfortunately means we can't move the data out,
            // which we could in the epoch versions.

            // XXX: oh hey, it's actually *way* worse than that.  It
            // could get dequeued and reused while we are copying it
            // out. This is a straight-up data race and I think there
            // is no way around it but to store the payload in a
            // std::atomic. That's gonna suck.
            ret = next->data_;
            if (this->head_.compare_exchange_weak(head, head.update(next))) {
                break;
            }
        }
    }

    // OK, everything set up.
    // head can be freed
    // head->data_ = optional<T>{}; // destroy object in node we free
    freelist_.unlinked(head);

    return ret;
}

}


#endif
