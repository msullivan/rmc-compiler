// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_TSTACK_LOCK
#define RMC_TSTACK_LOCK

// This is not *actually* a Trieber stack
// We just use a single mutex.

#include <mutex>
#include <utility>
#include "util.hpp"

namespace rmclib {

template<typename T>
class TStack {
private:
    struct TStackNode {
        TStackNode *next_{nullptr};
        optional<T> data_;

        TStackNode(T &&t) : data_(std::move(t)) {} // is this right
        TStackNode(const T &t) : data_(t) {}
    };

    std::mutex lock_;

    alignas(kCacheLinePadding)
    TStackNode *head_{nullptr};

    void pushNode(TStackNode *node);

public:
    TStack() { }

    optional<T> pop();

    void push(T &&t) {
        pushNode(new TStackNode(std::move(t)));
    }
    void push(const T &t) {
        pushNode(new TStackNode(t));
    }
};

template<typename T>
void TStack<T>::pushNode(TStackNode *node) {
    std::lock_guard<std::mutex> lock(lock_);
    node->next_ = head_;
    head_ = node;
}

template<typename T>
optional<T> TStack<T>::pop() {
    std::unique_lock<std::mutex> lock(lock_);

    TStackNode *head = head_;
    if (!head) return optional<T>{};

    head_ = head->next_;
    optional<T> ret(std::move(head->data_));
    head->data_ = optional<T>{}; // destroy the object

    lock.unlock();
    delete head; // delete old head

    return ret;
}

}


#endif
