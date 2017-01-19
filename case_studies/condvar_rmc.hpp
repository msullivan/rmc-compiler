// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef CONDVAR_RMC_H
#define CONDVAR_RMC_H

#include <rmc++.h>
#include <utility>
#include <cstdint>
#include "futex.hpp"

namespace rmclib {

// A cvar implementation that implements /much/ of the
// condition_variable_any interface and uses futexes.

// Requirements:
// wait can be viewed as three atomic parts
//  1. unlock and deschedule
//  2. wakeup
//  3. reacquire lock
//
// Executions of three parts of wait, notify_one, notify_all can be
// given a total order consistent with visible-to (XXX: union po??).
//
// I have not worked out all of the consequences of that, but one of
// the key ones is that if a wait.1 occurs in this order before a
// notify_one and there isn't any intervening (spurious) wakeup or
// another notify_one, it needs to wake that one up.
//
// It also means that if a notifying thread acquires the lock after a
// wait.1 and then notifies, that notify must come after the wait.1.
//
// A notify does /not/ need to "synchronize with" the waiter it wakes
// up, but the lock reacquisition will "synchronize with" the release
// in the notifier!

// XXX: *not* self-synchronizing on destruction with notify_all!
class condition_variable_futex {
private:
    Futex seq_;
    rmc::atomic<int> waiters_{0};

    rmc::atomic<int32_t> &rseq() {
        return reinterpret_cast<rmc::atomic<int32_t> &>(seq_.val);
    }

public:
    condition_variable_futex() {}
    condition_variable_futex(const condition_variable_futex&) = delete;
    condition_variable_futex& operator=(const condition_variable_futex&) =
        delete;


    // OK, I think we consider a wait.1 to cv-hb a notify iff the
    // inc_waiters is visible-to* chk_waiters.

    // This is a kind of weird implementation that admits spurious
    // wakeups but is very simple.
    template<class Lock>
    void wait(Lock &lock) {
        // read_seq needs to execute first so that we can't read the
        // signal from any notify that reads our inc_waiters
        XEDGE(read_seq, inc_waiters);
        // the inc_waiters must be visible-to the chk_waiters in a
        // notifier that // acquires this lock.
        //XEDGE(inc_waiters, unlock); // REDUNDANT

        // XXX: do we need an edge going into block???
        // It would be redundant, but...
        // I don't /think/ so, actually... Maybe from read_seq, but
        // in our RMW-futex model that is implied...

        // blocking needs to be done before dec_waiters and lock,
        // which could stop it from working
        //XEDGE(block, dec_waiters); // REDUNDANT
        //XEDGE(block, lock); // REDUNDANT

        int seq = L(read_seq, rseq());
        L(inc_waiters, waiters_.fetch_add(1));

        LS(unlock, lock.unlock());

        do {
            LS(block, seq_.wait(seq));
            // I think that we shouldn't need a constraint to this
            // next read because worst case we just loop around again
            // and fail from the futex again...
        } while (rseq() == seq);


        L(dec_waiters, waiters_.fetch_add(-1));

        LS(lock, lock.lock());
    }

    void notify_one() {
        // The actual increment and wait needs to be xo after the
        // waiters check to make sure that waits that are cv-hb this
        // can't see the increment (and thus potentially miss the
        // signal)
        XEDGE(chk_waiters, signal);
        //XEDGE(chk_waiters, wake); // REDUNDANT
        // The updated seq value needs to be visible to anything we wake.
        // This is actually doubly redundant though if we are modeling
        // futexes as being RMWs to the futex location...
        //VEDGE(signal, wake); // REDUNDANT

        if (L(chk_waiters, waiters_) == 0) return;
        auto handle = seq_.getHandle();
        L(signal, rseq().fetch_add(1));
        LS(wake, Futex::wake(handle, 1));
    }

};

}

#endif
