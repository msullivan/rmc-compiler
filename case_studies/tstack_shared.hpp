// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_TSTACK_SHARED
#define RMC_TSTACK_SHARED

// An UnderlyingTStack must have already been defined.

#include <atomic>
#include <utility>
#include "util.hpp"
#include "epoch.hpp"

namespace rmclib {

// I'm doing this all C++ified, but maybe I shouldn't be.
template<typename T>
class TStack {
private:
    using Stack = UnderlyingTStack<optional<T>>;
    using TStackNode = typename Stack::TStackNode;
    Stack stack_;

    void pushNode(TStackNode *node) {
        auto guard = Epoch::pin();
        stack_.pushNode(node);
    }

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
optional<T> TStack<T>::pop() {
    auto guard = Epoch::pin();
    lf_ptr<TStackNode> node = stack_.popNode();

    if (!node) return optional<T>{};

    optional<T> ret(std::move(node->data_));
    node->data_ = optional<T>{}; // destroy the object
    guard.unlinked(node);

    return ret;
}

}

#endif
