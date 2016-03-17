#ifndef RMC_TSTACK_GEN_SC
#define RMC_TSTACK_GEN_SC

// This is a Treiber stack that uses generation counts on pointers to
// prevent ABA and has nodes that are unsafe to ever properly free.

// The idea is that you would use per-datastructure-type
// Treiber-stack-based freelists to reuse nodes in other sorts of data
// structures.

#include <atomic>
#include <utility>
#include "gen_ptr.hpp"
#include "util.hpp"

namespace rmclib {

template<typename T>
class TStackGen {
public:
    struct TStackNode;
    using NodePtr = gen_ptr<TStackNode *>;

    struct TStackNode {
        std::atomic<TStackNode *> next_;
        T data_;

        TStackNode(T &&t) : data_(std::move(t)) {}
        TStackNode(const T &t) : data_(t) {}

        T data() const { return data_; }
    };


private:
    alignas(kCacheLinePadding)
    std::atomic<NodePtr> head_;

public:
    void pushNode(TStackNode *node);
    TStackNode *popNode();

    TStackGen() { }
};

template<typename T>
void TStackGen<T>::pushNode(TStackNode *node) {
    NodePtr oldHead = head_;
    for (;;) {
        node->next_ = oldHead;
        if (head_.compare_exchange_weak(oldHead, oldHead.update(node))) break;
    }
}

template<typename T>
typename TStackGen<T>::TStackNode *TStackGen<T>::popNode() {
    NodePtr head = this->head_;;
    for (;;) {
        if (head == nullptr) {
            return nullptr;
        }
        TStackNode *next = head->next_;

        if (this->head_.compare_exchange_weak(head, head.update(next))) {
            break;
        }
    }

    return head;
}

}

#endif
