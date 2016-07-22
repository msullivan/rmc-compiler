// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_MS_QUEUE_LOCK
#define RMC_MS_QUEUE_LOCK

// This is not *actually* a Michael-Scott queue.
// We just use a single mutex.

#include <mutex>
#include <utility>
#include "util.hpp"

namespace rmclib {

template<typename T>
class MSQueue {
private:
    struct MSQueueNode {
        MSQueueNode *next_{nullptr};
        optional<T> data_;

        MSQueueNode() {} // needed for allocating dummy
        MSQueueNode(T &&t) : data_(std::move(t)) {} // is this right
        MSQueueNode(const T &t) : data_(t) {}
    };

    std::mutex lock_;

    alignas(kCacheLinePadding)
    MSQueueNode *head_{nullptr};
    alignas(kCacheLinePadding)
    MSQueueNode *tail_{nullptr};

    void enqueue_node(MSQueueNode *node);

public:
    MSQueue() {
        // Need to create a dummy node!
        // we don't really need a dummy, but it matches the 2lock
        // better and eliminates a bit of nonsense
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
void MSQueue<T>::enqueue_node(MSQueueNode *node) {
    std::lock_guard<std::mutex> lock(lock_);
    tail_->next_ = node;
    tail_ = node;
}

template<typename T>
optional<T> MSQueue<T>::dequeue() {
    std::unique_lock<std::mutex> lock(lock_);

    MSQueueNode *head = head_;
    MSQueueNode *next = head->next_;
    if (!next) return optional<T>{};

    head_ = next;
    optional<T> ret(std::move(next->data_));
    next->data_ = optional<T>{}; // destroy the object

    lock.unlock();
    delete head; // delete old head

    return ret;
}

}


#endif
