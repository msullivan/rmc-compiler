// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_FREELIST_SHARED
#define RMC_FREELIST_SHARED

#include <utility>
#include <vector>
#include <cstddef>
#include "util.hpp"

// Generic Treiber stack based typed freelist. Memory can be reclaimed
// but only as the same sort of object it was originally. This
// property is important for many lock-free algorithms.

namespace rmclib {
/////////////////////////////

// Is making this look like an Allocator or something worth our time?

template<typename T>
class Freelist {
private:
    FreelistTStackGen<T> freestack_;
    using Node = typename FreelistTStackGen<T>::TStackNode;

public:
    // Allocate a new node. If we need to actually create a new one,
    // it will be constructed with the default constructor. If we
    // reuse an existing node, it will be in whatever state it was in
    // when it was freed (unless racing threads modified it after that
    // point, which is probably bad.)
    T *alloc() {
        Node *node = freestack_.popNode();
        if (!node) node = new Node();
        return &node->data_;
    }

    void unlinked(T *ptr) {
        Node *node = container_of(ptr, &Node::data_);
        freestack_.pushNode(node);
    }
};


//////

}

#endif
