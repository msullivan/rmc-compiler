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

    void push_node(lf_ptr<TStackNode> node);

public:
    TStack() { }

    optional<T> pop();

    void push(T &&t) {
        push_node(new TStackNode(std::move(t)));
    }
    void push(const T &t) {
        push_node(new TStackNode(t));
    }
};

template<typename T>
void TStack<T>::push_node(lf_ptr<TStackNode> node) {
    auto guard = Epoch::pin();

    // Push the node onto a Treiber stack
    for (;;) {
        lf_ptr<TStackNode> head = head_;
        node->next_ = head;
        if (head_.compare_exchange_weak(head, node)) break;
    }
}

template<typename T>
optional<T> TStack<T>::pop() {
    auto guard = Epoch::pin();

    lf_ptr<TStackNode> head;

    for (;;) {
        head = this->head_;
        if (head == nullptr) {
            return optional<T>{};
        }
        lf_ptr<TStackNode> next = head->next_;

        if (this->head_.compare_exchange_weak(head, next)) {
            break;
        }
    }

    // OK, everything set up.
    // next contains the value we are reading
    // head can be freed
    optional<T> ret(std::move(head->data_));
    head->data_ = optional<T>{}; // destroy the object
    guard.unlinked(head);

    return ret;
}

}


#endif
