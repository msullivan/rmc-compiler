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

#include <rmc++.h>
#include <utility>
#include "util.hpp"

namespace rmclib {

template<typename T>
class UnsafeTStack {
public:
    struct TStackNode {
        rmc::atomic<TStackNode *> next_{nullptr};
        T data_;

        TStackNode(T &&t) : data_(std::move(t)) {} // is this right
        TStackNode(const T &t) : data_(t) {}
    };


private:
    alignas(kCacheLinePadding)
    rmc::atomic<TStackNode *> head_{nullptr};

public:
    void pushStack(TStackNode *head, TStackNode *tail);
    void pushNode(TStackNode *node);
    TStackNode *popNode();
    TStackNode *popAll() {
        VEDGE(popall, post);

        if (!head_) return nullptr;
        return L(popall, head_.exchange(nullptr));
    }

    UnsafeTStack() { }
};

template<typename T>
void UnsafeTStack<T>::pushNode(TStackNode *node) {
    pushStack(node, node);
}

template<typename T>
void UnsafeTStack<T>::pushStack(TStackNode *head, TStackNode *tail) {
    // Don't need edge from head_ load because 'push' will also
    // read from the write to head.

    VEDGE(node_setup, push);

    LPRE(node_setup);

    for (;;) {
        TStackNode *oldHead = head_;
        L(node_setup, tail->next_ = oldHead);
        if (L(push, head_.compare_exchange_weak(oldHead, head))) break;
    }
}

template<typename T>
typename UnsafeTStack<T>::TStackNode *UnsafeTStack<T>::popNode() {
    // We don't need any constraints going /into/ 'pop'; all the data
    // in nodes is published by writes into head_, and the CAS reads-from
    // and writes to head_, perserving visibility.
    XEDGE(read_head, read_next);
    XEDGE(pop, out);

    TStackNode *head;

    for (;;) {
        head = L(read_head, this->head_);
        if (head == nullptr) {
            return nullptr;
        }
        // XXX: edge think about now
        TStackNode *next = L(read_next, head->next_);

        if (L(pop, this->head_.compare_exchange_weak(head, next))) {
            break;
        }
    }

    LPOST(out);

    return head;
}

}

#undef UnsafeTStack
