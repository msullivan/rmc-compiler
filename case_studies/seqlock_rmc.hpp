// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef SEQLOCK_SC_H
#define SEQLOCK_SC_H

#include <rmc++.h>
#include <utility>
#include "util.hpp"

namespace rmclib {

/// BEGIN SNIP
class SeqLock {
private:
    rmc::atomic<uintptr_t> count_{0};
/// END SNIP
    void delay() { }
/// BEGIN SNIP
    bool is_locked(uintptr_t tag) { return (tag & 1) != 0; }

public:
    using Tag = uintptr_t;

    Tag read_lock() {
        // Lock acquisition needs to execute before the critical section
        XEDGE(read, post);
        return L(read, count_);
    }

    bool read_unlock(Tag tag) {
        // The body of the critical section needs to execute before
        // the unlock check, because if we observed any writes from a
        // writer critical section, it is important that we also
        // observe the lock.
        XEDGE(pre, check);
        return !is_locked(tag) && L(check, count_) == tag;
    }

    void write_lock() {
        // Lock acquisition needs to execute before the critical
        // section BUT: This one is visibility! If a reader observes a
        // write in the critical section, it is important that it also
        // observes the lock.
        VEDGE(acquire, out);

        for (;;) {
            Tag tag = count_;
            if (!is_locked(tag) &&
                L(acquire, count_.compare_exchange_weak(tag, tag+1))) {
                break;
            }
            delay();
        }
        LPOST(out);
    }

    void write_unlock() {
        VEDGE(pre, release);
        uintptr_t newval = count_ + 1;
        L(release, count_ = newval);
    }
};
/// END SNIP


}

#endif
