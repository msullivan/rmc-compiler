#ifndef CONDVAR_SC_H
#define CONDVAR_SC_H

#include <atomic>
#include <utility>
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
// given a total order consistent with happens-before.
//
// I have not worked out all of the consequences of that, but one of
// the key ones is that if a wait.1 occurs in this order before a
// notify_one and there isn't any intervening (spurious) wakeup or
// another notify_one, it needs to wake that one up.
//
// It also means that if a notifying thread acquires the lock after a
// wait.1 and then notifies, that notify must come after the wait.1.

// XXX: *not* self-synchronizing on destruction with notify_all!
class condition_variable_futex {
private:
    Futex seq_;
    std::atomic<int> waiters_{0};

public:
    condition_variable_futex() {}
    condition_variable_futex(const condition_variable_futex&) = delete;
    condition_variable_futex& operator=(const condition_variable_futex&) =
        delete;

    // This is a kind of weird implementation that admits spurious
    // wakeups but is very simple.
    template<class Lock>
    void wait(Lock &lock) {
        int seq = seq_.val;
        waiters_.fetch_add(1);

        lock.unlock();

        do {
            seq_.wait(seq);
        } while (seq_.val == seq);

        waiters_.fetch_add(-1);

        lock.lock();
    }

    void notify_one() {
        if (waiters_ == 0) return;
        auto handle = seq_.getHandle();
        seq_.val.fetch_add(1);
        Futex::wake(handle, 1);
    }

};

}

#endif
