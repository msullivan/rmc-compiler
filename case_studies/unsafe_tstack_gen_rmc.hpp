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
    rmc::atomic<gen_ptr<TStackNode *>> head_;

public:
    void pushNode(TStackNode *node);
    TStackNode *popNode();

    UnsafeTStackGen() { }
};

//// SNIP ////
template<typename T>
rmc_noinline
void UnsafeTStackGen<T>::pushNode(TStackNode *node) {
    // Don't need edge from head_ load because 'push' will also
    // read from the write to head.

    VEDGE(node_setup, push);
    VEDGE(node_next, push);

    LPRE(node_setup);

    gen_ptr<TStackNode *> oldHead = head_;
    for (;;) {
        L(node_next, node->next_ = oldHead);
        if (L(push, head_.compare_exchange_weak(oldHead, oldHead.inc(node))))
            break;
    }
}

template<typename T>
rmc_noinline
typename UnsafeTStackGen<T>::TStackNode *UnsafeTStackGen<T>::popNode() {
    // We don't need any constraints going /into/ the pop; all the data
    // in nodes is published by writes into head_, and the CAS reads-from
    // and writes to head_, perserving visibility.
    XEDGE(read_head, read_next);
    XEDGE(read_head, out);

    gen_ptr<TStackNode *> head = L(read_head, this->head_);
    for (;;) {
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = L(read_next, head->next_);

        if (L(read_head, this->head_.compare_exchange_weak(head,
                                                           head.inc(next)))){
            break;
        }
    }

    LPOST(out);

    return head;
}
//// SNIP ////

}

#undef UnsafeTStackGen
