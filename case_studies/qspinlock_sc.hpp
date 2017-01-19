// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef QSPINLOCK_SC_H
#define QSPINLOCK_SC_H

#include <atomic>
#include <utility>
#include "tagged_ptr.hpp"

namespace rmclib {

// This is based on "MCS locks" and linux's qspinlocks.  The key idea
// here, and the reason for the extra complication relative to
// ticket-taking spinlocks, is that different threads can spin-wait on
// different locations, reducing cache contention.

class QSpinLock {
    // Aligned to 256 so the
    struct alignas(256) Node {
        using Ptr = tagged_ptr<Node *>;
        std::atomic<Node *> next{nullptr};
        std::atomic<bool> ready{false};
    };

    std::atomic<Node::Ptr> tail_;

    // This should be yield() or cpu_relax() or something
    void delay() { }

    void clearTag(std::atomic<Node::Ptr> &loc) {
        // We want to just xadd(-1) the thing, but C++ doesn't let us
        // because of the level of obstruction^Wabstraction that
        // tagged_ptr adds.
        //
        // Or maybe what we want to do is to align Node on 256 boundaries
        // so that we can do a one byte write to clear the locked flag.
        // That is *especially* not a thing in the C++ memory model.
        //
#if CLEAR_XADD
        // This is probably undefined
        auto &intloc = reinterpret_cast<std::atomic<uintptr_t> &>(loc);
        intloc.fetch_sub(1);
#elif CLEAR_BYTE_WRITE
        // This is certainly undefined, and only works on little endian
        // C++ really does not have any story for mixed-size atomics
        // and mixed-size atomics are pretty funky in practice.
        // Linux does do this on some platforms, though.
        auto &byteloc = reinterpret_cast<std::atomic<uint8_t> &>(loc);
        byteloc = 0;
#else
        Node::Ptr state(nullptr, 1);
        while (!loc.compare_exchange_strong(state, Node::Ptr(state, 0))) {
            delay();
        }
#endif
    }

    void slowpathLock(Node::Ptr oldTail) {
        //printf("into slowpath!\n");

        Node me;
        Node::Ptr curTail;
        bool newThreads;

        // Step one, put ourselves at the back of the queue
        for (;;) {
            Node::Ptr newTail = Node::Ptr(&me, oldTail.tag());

            // Enqueue ourselves...
            if (tail_.compare_exchange_strong(oldTail, newTail)) break;

            // OK, maybe the whole thing is just unlocked now?
            if (oldTail == Node::Ptr(nullptr, 0)) {
                // If so, try the top level lock
                if (tail_.compare_exchange_strong(oldTail,
                                                  Node::Ptr(nullptr, 1)))
                    goto out;
            }

            delay();
        }

        // Step two: OK, there is an actual queue, so link up with the old
        // tail and wait until we are at the head of the queue
        if (oldTail.ptr()) {
            // * Writing into the oldTail is safe because threads can't
            //   leave unless there is no thread after them or they have
            //   marked the next ready
            oldTail->next = &me;

            while (!me.ready) delay();
        }

        // Step three: wait until the lock is freed
        while ((curTail = tail_).tag()) {
            delay();
        }

        // Step four: take the lock
        for (;;) {
            assert_eq(curTail.tag(), 0);
            assert_ne(curTail.ptr(), nullptr);

            newThreads = curTail.ptr() != &me;

            // If there aren't any waiters after us, the queue is
            // empty. Otherwise, keep the old tail.
            Node *newTailP = newThreads ? curTail : nullptr;
            Node::Ptr newTail = Node::Ptr(newTailP, 1);

            if (tail_.compare_exchange_strong(curTail, newTail)) break;
        }

        // Step five: now that we have the lock, if any threads came
        // in after us, indicate to the next one that it is at the
        // head of the queue
        if (newThreads) {
            // Next thread might not have written itself in, yet,
            // so we have to wait.
            Node *next;
            while (!(next = me.next)) delay();
            next->ready = true;
        }

        //printf("full slowpath out\n");
    out:
        //printf("made it out of slowpath!\n");
        return;
    }

public:
    void lock() {
        Node::Ptr unlocked(nullptr, 0);
        if (!tail_.compare_exchange_strong(unlocked, Node::Ptr(nullptr, 1))) {
            slowpathLock(unlocked);
        }
    }
    void unlock() {
        clearTag(tail_);
    }

};

}

#endif
