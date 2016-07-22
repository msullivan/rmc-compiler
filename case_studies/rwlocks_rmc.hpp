// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RWLOCKS_SC_H
#define RWLOCKS_SC_H

#include <rmc++.h>
#include <utility>

namespace rmclib {

// This can starve readers but not writers, I think
class busy_rwlock_readstarve {
    const uintptr_t kWriterLocked = ~(~0u >> 1);
    rmc::atomic<uintptr_t> locked_{0};

    void delay() { }
    bool writeLocked(uintptr_t locked) { return (locked & kWriterLocked) != 0; }
    bool readLocked(uintptr_t locked) { return (locked & ~kWriterLocked) != 0; }

public:
    void lock_shared() {
        XEDGE(lock, out);

        // Optimistic lock attempt
        uintptr_t locked = L(lock, locked_.fetch_add(1));
        if (!writeLocked(locked)) goto out;

        // That didn't work. Back off and do the slowpath.
        locked_.fetch_sub(1);
        for (;;) {
            if (!writeLocked(locked)) {
                if (L(lock,
                      locked_.compare_exchange_weak(locked, locked+1))) break;
            } else {
                locked = locked_;
            }
        }

    out:
        LPOST(out);
    }
    void unlock_shared() {
        VEDGE(pre, unlock);
        L(unlock, locked_.fetch_sub(1));
    }

    void lock() {
        XEDGE(acquire, out);

        uintptr_t locked;
        for (;;) {
            locked = locked_;
            if (!writeLocked(locked)) {
                if (L(acquire, locked_.compare_exchange_weak(
                          locked, locked|kWriterLocked))) {
                    break;
                }
            }
        }
        while (readLocked(locked)) {
            locked = L(acquire, locked_);
        }

        LPOST(out);
    }

    void unlock() {
        VEDGE(pre, unlock);
        L(unlock, locked_.fetch_xor(kWriterLocked));
    }
};

class busy_rwlock_writestarve {
    const uintptr_t kWriterLocked = ~(~0u >> 1);
    rmc::atomic<uintptr_t> locked_{0};

    void delay() { }
    bool writeLocked(uintptr_t locked) { return (locked & kWriterLocked) != 0; }
    bool readLocked(uintptr_t locked) { return (locked & ~kWriterLocked) != 0; }

public:
    void lock_shared() {
        // N.B: the fetch_add is prior to any of the reads from
        // locked_
        XEDGE(lock, out);
        uintptr_t locked = L(lock, locked_.fetch_add(1));
        while (writeLocked(locked)) {
            locked = L(lock, locked_);
        }
        LPOST(out);
    }
    void unlock_shared() {
        VEDGE(pre, unlock);
        L(unlock, locked_.fetch_sub(1));
    }

    void lock() {
        XEDGE(lock, out);
        for (;;) {
            uintptr_t locked = locked_;
            if (locked == 0) {
                if (L(lock,
                      locked_.compare_exchange_weak(locked, kWriterLocked))) {
                    break;
                }
            }
        }
        LPOST(out);
    }

    void unlock() {
        VEDGE(pre, unlock);
        L(unlock, locked_.fetch_xor(kWriterLocked));
    }
};

}

#endif
