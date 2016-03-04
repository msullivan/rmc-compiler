#ifndef SEQLOCK_SC
#define SEQLOCK_SC

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {
#if 0
} // f. this
#endif

class SeqLock {
private:
    std::atomic<uintptr_t> count_{0};

public:
    using Tag = uintptr_t;

    void delay() { }

    bool is_locked(Tag tag) { return (tag & 1) != 0; }

    Tag read_lock() {
        Tag tag;
        while (is_locked((tag = count_))) {
            delay();
        }
        return tag;
    }

    bool read_unlock(Tag tag) {
        return count_ == tag;
    }

    void write_lock() {
        for (;;) {
            Tag tag = count_;
            if (!is_locked(tag) && count_.compare_exchange_weak(tag, tag+1)) {
                break;
            }
            delay();
        }
    }
    void write_unlock() {
        count_ = count_ + 1;
    }
};


}

#endif
