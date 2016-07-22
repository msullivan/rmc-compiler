// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef SEQLOCK_SC_H
#define SEQLOCK_SC_H

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {

// WAIT THIS IS ACTUALLY FUCKED
// read_unlocked is sort of a mythical "release load"
// mo_rlx reads in the critical section could get reordered past the
// release read

class SeqLock {
public:
    using Tag = uintptr_t;

private:
    std::atomic<uintptr_t> count_{0};

    void delay() { }
    bool is_locked(Tag tag) { return (tag & 1) != 0; }

public:
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
