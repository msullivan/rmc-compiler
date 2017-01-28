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

#include <rmc++.h>
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
        rmc::atomic<TStackNode *> next_;
        T data_;

        TStackNode() : data_() {}
        TStackNode(T &&t) : data_(std::move(t)) {}
        TStackNode(const T &t) : data_(t) {}

        T data() const { return data_; }
    };


private:
    alignas(kCacheLinePadding)
    rmc::gen_atomic<TStackNode *> head_;

public:
    void pushNode(TStackNode *node);
    TStackNode *popNode();

    UnsafeTStackGen() { }
};

template<typename T>
rmc_noinline
void UnsafeTStackGen<T>::pushNode(TStackNode *node) {
    // Don't need edge from head_ load because 'push' will also
    // read from the write to head.

    VEDGE(node_setup, push);

    LPRE(node_setup);

    for (;;) {
        NodePtr oldHead = head_;
        L(node_setup, node->next_ = oldHead);
        if (L(push, head_.compare_exchange_weak_gen(oldHead, node)))
            break;
    }
}

template<typename T>
rmc_noinline
typename UnsafeTStackGen<T>::TStackNode *UnsafeTStackGen<T>::popNode() {
    // We don't need any constraints going /into/ 'pop'; all the data
    // in nodes is published by writes into head_, and the CAS reads-from
    // and writes to head_, perserving visibility.
    XEDGE(read_head, read_next);
    XEDGE(pop, out);

    NodePtr head;
    for (;;) {
        head = L(read_head, this->head_);
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = L(read_next, head->next_);

        if (L(pop, this->head_.compare_exchange_weak_gen(head, next))){
            break;
        }
    }

    LPOST(out);

    return head;
}

}

#undef UnsafeTStackGen
