#ifndef RMC_TSTACK_SHARED2
#define RMC_TSTACK_SHARED2

// The generic code for high level Treiber stacks based on
// freelists and generation counts (rather than epochs).

// TStackGen and Freelist must already be defined

#include <utility>
#include "util.hpp"

#include "freelist.hpp"

namespace rmclib {

template<typename T>
class TStack {
private:
    using Stack = TStackGen<optional<T>>;
    using TStackNode = typename Stack::TStackNode;

    Stack stack_;
    Freelist<TStackNode> freelist_; // XXX: should be global :( :( :(

public:
    TStack() { }

    optional<T> pop();

    void push(T &&t) {
        auto node = freelist_.alloc();
        node->data_ = std::move(t);
        stack_.pushNode(node);
    }
    void push(const T &t) {
        auto node = freelist_.alloc();
        node->data_ = t;
        stack_.pushNode(node);
    }
};

template<typename T>
optional<T> TStack<T>::pop() {
    lf_ptr<TStackNode> node = stack_.popNode();

    if (!node) return optional<T>{};

    optional<T> ret(std::move(node->data_));
    node->data_ = optional<T>{}; // destroy the object
    freelist_.unlinked(node);

    return ret;
}

}

#endif
