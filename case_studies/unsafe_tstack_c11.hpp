// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// N.B.: This doesn't have header guards. It should be included with a
// new name for UnsafeTStackNode defined.  This is so that different
// versions of UnsafeTStack can be used in the same program for
// testing (for example, an rmc version in Epoch and an sc version in
// TStack).

// This is one way in which it is unsafe. It is also unsafe because
// the caller is responsible for handling memory management safely.

#ifndef UnsafeTStack
#error UnsafeTStack must be defined to some other name
#endif

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {

template<typename T>
class UnsafeTStack {
public:
    struct TStackNode {
        std::atomic<TStackNode *> next_{nullptr};
        T data_;

        TStackNode(T &&t) : data_(std::move(t)) {} // is this right
        TStackNode(const T &t) : data_(t) {}
    };


private:
    alignas(kCacheLinePadding)
    std::atomic<TStackNode *> head_{nullptr};

    static const std::memory_order mo_rlx = std::memory_order_relaxed;
    static const std::memory_order mo_rel = std::memory_order_release;
    static const std::memory_order mo_acq = std::memory_order_acquire;
    static const std::memory_order mo_acq_rel = std::memory_order_acq_rel;

public:
    void pushStack(TStackNode *head, TStackNode *tail);
    void pushNode(TStackNode *node);
    TStackNode *popNode();
    TStackNode *popAll() {
        if (!head_.load(mo_rlx)) return nullptr;
        // shouldn't need release because we aren't publishing anything
        // do need acquire since we care about the contents we popped
        return head_.exchange(nullptr, mo_acq);
    }

    UnsafeTStack() { }
};

template<typename T>
void UnsafeTStack<T>::pushNode(TStackNode *node) {
    pushStack(node, node);
}

template<typename T>
void UnsafeTStack<T>::pushStack(TStackNode *head, TStackNode *tail) {
    for (;;) {
        TStackNode *oldHead = head_.load(mo_acq);
        // XXX: could use acq_rel fence?
        // but I think the compiler can handle it.
        // XXX: should we try to use the loads in the compare/exchange
        // We do in the gen count version
        tail->next_.store(oldHead, mo_rel);
        if (head_.compare_exchange_weak(oldHead, head,
                                        mo_rel, mo_rlx)) break;
    }
}

template<typename T>
typename UnsafeTStack<T>::TStackNode *UnsafeTStack<T>::popNode() {
    TStackNode *head;

    for (;;) {
        head = this->head_.load(mo_acq);
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = head->next_.load(mo_acq);

        if (this->head_.compare_exchange_weak(head, next,
                                              mo_rel, mo_rlx)) {
            break;
        }
    }

    return head;
}

}

#undef UnsafeTStack
