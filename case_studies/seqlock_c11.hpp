// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef SEQLOCK_C11_H
#define SEQLOCK_C11_H

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {

// This is based on "Can Seqlocks Get Along With Programming Language
// Memory Models" by Hans Boehm

/// BEGIN SNIP
class SeqLock {
private:
    std::atomic<uintptr_t> count_{0};

/// END SNIP
    void delay() { }
/// BEGIN SNIP
    bool is_locked(uintptr_t tag) { return (tag & 1) != 0; }

public:
    using Tag = uintptr_t;

    Tag read_lock() {
        return count_.load(std::memory_order_acquire);
    }

    bool read_unlock(Tag tag) {
        // acquire fence is to ensure that if we read any writes
        // from a writer's critical section, we also see the writer's
        // acquisition of the lock
        std::atomic_thread_fence(std::memory_order_acquire);
        return !is_locked(tag) && count_.load(std::memory_order_relaxed) == tag;
    }

    void write_lock() {
        for (;;) {
            Tag tag = count_;
            if (!is_locked(tag) && count_.compare_exchange_weak(tag, tag+1)) {
                break;
            }
            delay();
        }
        // release fense ensures that anything that sees our critical
        // section writes can see our lock acquisition
        std::atomic_thread_fence(std::memory_order_release);
    }
    void write_unlock() {
        uintptr_t newval = count_.load(std::memory_order_relaxed) + 1;
        count_.store(newval, std::memory_order_release);
    }
};
/// END SNIP

}

#endif
