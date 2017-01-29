// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// N.B.: This doesn't have header guards. It should be included with a
// new name for UnsafeTStackNode defined.  This is so that different
// versions of UnsafeTStack can be used in the same program for
// testing (for example, an rmc version in Freelist and an sc version in
// TStack).

// This is one way in which it is unsafe. It is also unsafe because
// the caller is responsible for handling memory management safely.

#ifndef UnsafeTStackGen
#error UnsafeTStackGen must be defined to some other name
#endif

#include <atomic>
#include <utility>
#include "gen_ptr.hpp"
#include "util.hpp"

namespace rmclib {

template<typename T>
class UnsafeTStackGen {
public:
    struct TStackNode;
    using NodePtr = gen_ptr<TStackNode *>;

    struct TStackNode {
        std::atomic<TStackNode *> next_;
        T data_;

        TStackNode() : data_() {}
        TStackNode(T &&t) : data_(std::move(t)) {}
        TStackNode(const T &t) : data_(t) {}

        T data() const { return data_; }
    };


private:
    alignas(kCacheLinePadding)
    std::gen_atomic<TStackNode *> head_;

public:
    void pushNode(TStackNode *node);
    TStackNode *popNode();

    UnsafeTStackGen() { }
};

template<typename T>
rmc_noinline
void UnsafeTStackGen<T>::pushNode(TStackNode *node) {
    NodePtr oldHead = head_.load(mo_rlx);
    for (;;) {
        node->next_.store(oldHead, mo_rlx);
        if (head_.compare_exchange_weak_gen(oldHead, node,
                                            mo_rel, mo_rlx)) break;
    }
}

template<typename T>
rmc_noinline
typename UnsafeTStackGen<T>::TStackNode *UnsafeTStackGen<T>::popNode() {
    NodePtr head = this->head_.load(mo_acq);
    for (;;) {
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = head->next_.load(mo_rlx);

        // We would like to make the success order mo_rlx,
        // but the success order can't be weaker than the failure one.
        if (this->head_.compare_exchange_weak_gen(head, next,
                                                  mo_acq, mo_acq)) {
            break;
        }
    }

    return head;
}

}

#undef UnsafeTStackGen
