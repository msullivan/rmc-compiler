#ifndef RMC_MS_QUEUE_RMC2
#define RMC_MS_QUEUE_RMC2

#include <rmc++.h>
#include <utility>
#include "util.hpp"
#include "gen_ptr.hpp"
#include "freelist.hpp"
#include "universal.hpp"

// A version of Michael-Scott queues that closely corresponds to the
// original version using generation pointers.

namespace rmclib {

struct MSQueueNode {
    rmc::gen_atomic<lf_ptr<MSQueueNode>> next_;
    // Traditional MS Queues need to read out the data from nodes
    // that may already be getting reused.
    // To avoid data races copying the elements around,
    // we instead store a pointer to an allocated object.
    rmc::atomic<universal> data_;

    static Freelist<MSQueueNode> freelist;
};
Freelist<MSQueueNode> MSQueueNode::freelist;

template<typename T>
class MSQueue {
private:
    using NodePtr = gen_ptr<lf_ptr<MSQueueNode>>;

    alignas(kCacheLinePadding)
    rmc::gen_atomic<lf_ptr<MSQueueNode>> head_;
    alignas(kCacheLinePadding)
    rmc::gen_atomic<lf_ptr<MSQueueNode>> tail_;

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
void MSQueue<T>::enqueueNode(lf_ptr<MSQueueNode> node) {
    node->next_ = node->next_.load().update(nullptr); // XXX: ok?

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

    // Make sure the read out of next executes before the verification
    // that it read from a sensible place:
    XEDGE(get_next, verify_tail);


    // Marker for node initialization. Everything before the
    // enqueue_node call is "init".
    LPRE(node_init);

    NodePtr tail, next;

    for (;;) {
        tail = L(get_tail, this->tail_);
        next = L(get_next, tail->next_);
        // Check that tail and next are consistent: In this gen
        // counter version, this is important for correctness: if,
        // after we read the tail, it gets removed from this queue,
        // freed, and added to some other queue, we need to make sure
        // that we don't try to append to that queue instead.
        if (tail != L(verify_tail, this->tail_)) continue;

        // was tail /actually/ the last node?
        if (next == nullptr) {
            // if so, try to write it in. (nb. this overwrites next)
            // XXX: does weak actually help us here?
            if (L(enqueue, tail->next_.compare_exchange_weak_gen(next, node))) {
                // we did it! return
                break;
            }
        } else {
            // nope. try to swing the tail further down the list and try again
            L(catchup_swing,
              this->tail_.compare_exchange_strong_gen(tail, next));
        }
    }

    // Try to swing the tail_ to point to what we inserted
    L(enqueue_swing,
      this->tail_.compare_exchange_strong_gen(tail, node));
}

template<typename T>
optional<T> MSQueue<T>::dequeue() {
    // Core message passing: reading the data out of the node comes
    // after getting the pointer to it.
    XEDGE(get_next, node_use);
    // Make sure we see at least head's init
    XEDGE(get_head, get_next);
    // Need to make sure anything visible through the next pointer
    // stays visible when it gets republished at the head or tail
    VEDGE(get_next, catchup_swing);
    VEDGE(get_next, dequeue);

    // Make sure the read out of next executes before the verification
    // that it read from a sensible place:
    XEDGE(get_next, verify_head);
    XEDGE(get_tail, verify_head); // XXX: think about more


    NodePtr head, tail, next;

    universal data;

    for (;;) {
        head = L(get_head, this->head_);
        tail = L(get_tail, this->tail_); // XXX: really?
        next = L(get_next, head->next_);

        // Consistency check; see note above
        if (head != L(verify_head, this->head_)) continue;

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
                  this->tail_.compare_exchange_strong_gen(tail, next));
            }
        } else {
            // OK, now we try to actually read the thing out.

            // We need to read the data out of the node
            // *before* we try to dequeue it or else it could get
            // reused before we read it out.
            data = L(node_use, next->data_);
            if (L(dequeue, this->head_.compare_exchange_weak_gen(head, next))) {
                break;
            }
        }
    }

    LPOST(node_use);

    // OK, everything set up.
    // head can be freed
    MSQueueNode::freelist.unlinked(head);
    optional<T> ret(data.extract<T>());

    return ret;
}

}


#endif
