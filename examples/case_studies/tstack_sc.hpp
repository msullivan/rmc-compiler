#ifndef RMC_TSTACK_SC
#define RMC_TSTACK_SC

#include <atomic>
#include <utility>
#include <experimental/optional>
#include "util.hpp"
#include "epoch.hpp"

namespace rmclib {

using std::experimental::optional;

// I'm doing this all C++ified, but maybe I shouldn't be.
template<typename T>
class TStack {
private:
    struct TStackNode {
        std::atomic<lf_ptr<TStackNode>> next_{nullptr};
        optional<T> data_;

        TStackNode() {} // needed for allocating dummy
        TStackNode(T &&t) : data_(std::move(t)) {} // is this right
        TStackNode(const T &t) : data_(t) {}
    };


    alignas(kCacheLinePadding)
    std::atomic<lf_ptr<TStackNode>> head_{nullptr};


    void pushStack(lf_ptr<TStackNode> head, lf_ptr<TStackNode> tail);
    void pushNode(lf_ptr<TStackNode> node);
    // XXX: need to be in an epoch when you call it
    lf_ptr<TStackNode> popNode();


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
void TStack<T>::pushNode(lf_ptr<TStackNode> node) {
    pushStack(node, node);
}

template<typename T>
void TStack<T>::pushStack(lf_ptr<TStackNode> head, lf_ptr<TStackNode> tail) {
    auto guard = Epoch::pin();

    // Push the node onto a Treiber stack
    for (;;) {
        lf_ptr<TStackNode> oldHead = head_;
        tail->next_ = oldHead;
        if (head_.compare_exchange_weak(oldHead, head)) break;
    }
}

template<typename T>
lf_ptr<typename TStack<T>::TStackNode> TStack<T>::popNode() {
    lf_ptr<TStackNode> head;

    for (;;) {
        head = this->head_;
        if (head == nullptr) {
            return nullptr;
        }
        lf_ptr<TStackNode> next = head->next_;

        if (this->head_.compare_exchange_weak(head, next)) {
            break;
        }
    }

    return head;
}

template<typename T>
optional<T> TStack<T>::pop() {
    auto guard = Epoch::pin();
    lf_ptr<TStackNode> node = popNode();

    if (!node) return optional<T>{};

    optional<T> ret(std::move(node->data_));
    node->data_ = optional<T>{}; // destroy the object
    guard.unlinked(node);

    return ret;
}

}


#endif
