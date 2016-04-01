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

public:
    void pushStack(TStackNode *head, TStackNode *tail);
    void pushNode(TStackNode *node);
    TStackNode *popNode();
    TStackNode *popAll() {
        if (!head_) return nullptr;
        return head_.exchange(nullptr);
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
        TStackNode *oldHead = head_;
        tail->next_ = oldHead;
        if (head_.compare_exchange_weak(oldHead, head)) break;
    }
}

template<typename T>
typename UnsafeTStack<T>::TStackNode *UnsafeTStack<T>::popNode() {
    TStackNode *head;

    for (;;) {
        head = this->head_;
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = head->next_;

        if (this->head_.compare_exchange_weak(head, next)) {
            break;
        }
    }

    return head;
}

}

#undef UnsafeTStack
