// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_PARKING_HPP
#define RMC_PARKING_HPP

// General mechanism for blocking threads. Based on the interface that
// Java and Rust use.

#include <chrono>

#if defined(__linux__)
#define USE_FUTEX_PARKING 1
#elif defined(__unix__)
#define USE_PTHREAD_PARKING 1
#endif

#if USE_FUTEX_PARKING

#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include "futex.hpp"

namespace rmclib {

class Parking {
private:
    struct ThreadNode {
        Futex flag;
    };
    static thread_local ThreadNode me_;

public:
    using ThreadID = ThreadNode*;
    static const constexpr ThreadID kEmptyId = nullptr;

    static ThreadID getCurrent() { return &me_; }

private:
    // XXX: think about more; we should maybe have a better spec...
    // Is the behavior we want this?:
    // There is a consistent total order of park()/unpark() actions.
    // Any unpark() that is before a park() in that order should
    // happen-before it
    // park() happens as it is leaving, I guess...
    template<typename Timeout>
    static inline void park_inner(Timeout timeout) {
        ThreadID me = getCurrent();
        // Doing an if allows spurious wakeups but means we don't need
        // any extra logic to handle timeouts.
        if (!me->flag.val) {
            me->flag.wait(0, timeout);
        }
        // Do the exchange so that we see all the writes we are overwriting
        // XXX: is there a better way??
        me->flag.val.exchange(0);
    }
public:

    static void unpark(ThreadID thread) {
        // exchange keeps it all together in a release sequence
        // and lets us avoid signalling if it is already up
        Futex::Handle handle = thread->flag.getHandle();
        if (thread->flag.val.exchange(1) != 1) {
            Futex::wake(handle, 1);
        }
    }

    // Park wrappers
    static void park() { park_inner(Futex::no_timeout); }

    template<class Rep, class Period>
    static void park_for(const std::chrono::duration<Rep, Period>& relTime) {
        park_inner(relTime);
    }

    template<class Clock, class Duration>
    static void park_until(
        const std::chrono::time_point<Clock, Duration>& timeout) {
        park_inner(timeout);
    }
};

}

#elif USE_PTHREAD_PARKING

// This is a really basic implementation of the parking scheme
// But using pthread stuff directly
// The advantage here is that no constructor needs to run for the
// thread local nodes.
#include <pthread.h>
#include "chrono_to_timespec.hpp"

namespace rmclib {

class Parking {
private:
    struct ThreadNode {
        pthread_mutex_t lock;
        pthread_cond_t cond;
        bool flag;
    };
    static thread_local ThreadNode me_;

public:
    using ThreadID = ThreadNode*;
    static const constexpr ThreadID kEmptyId = nullptr;

    static ThreadID getCurrent() { return &me_; }

private:
    template<typename F>
    static inline void park_inner(F wait) {
        ThreadID me = getCurrent();
        pthread_mutex_lock(&me->lock);
        // Doing an if allows spurious wakeups but means we don't need
        // any extra logic to handle timeouts.
        if (!me->flag) {
            wait(me);
        }
        me->flag = false;
        pthread_mutex_unlock(&me->lock);
    }
public:
    static void unpark(ThreadID thread) {
        pthread_mutex_lock(&thread->lock);
        if (!thread->flag) {
            thread->flag = true;
            // XXX unlock first
            // or maybe that doesn't work cause it could exit
            pthread_cond_signal(&thread->cond);
        }
        pthread_mutex_unlock(&thread->lock);

    }

    // Park wrappers
    static void park() {
        park_inner([](ThreadID me) {
                pthread_cond_wait(&me->cond, &me->lock);
        });
    }

    template<class Rep, class Period>
    static void park_for(const std::chrono::duration<Rep, Period>& relTime) {
        park_until(std::chrono::system_clock::now() + relTime);
    }

    template<class Clock, class Duration>
    static void park_until(
        const std::chrono::time_point<Clock, Duration>& timeout) {

        struct timespec ts = pointToTimeSpec(timeout);
        park_inner([&](ThreadID me) {
                pthread_cond_timedwait(&me->cond, &me->lock, &ts);
        });
    }
};

}

#else

// This is a really basic implementation of the parking scheme
// using C++ locks and cvars
#include <mutex>
#include <condition_variable>

namespace rmclib {

class Parking {
private:
    struct ThreadNode {
        std::mutex lock;
        std::condition_variable cond;
        bool flag{false};
    };
    static thread_local ThreadNode me_;

public:
    using ThreadID = ThreadNode*;
    static const constexpr ThreadID kEmptyId = nullptr;

    static ThreadID getCurrent() { return &me_; }

private:
    template<typename F>
    static inline void park_inner(F wait) {
        ThreadID me = getCurrent();
        std::unique_lock<std::mutex> lk(me->lock);
        // Doing an if allows spurious wakeups but means we don't need
        // any extra logic to handle timeouts.
        if (!me->flag) {
            wait(me, lk);
        }
        me->flag = false;
    }
public:

    static void unpark(ThreadID thread) {
        std::unique_lock<std::mutex> lk(thread->lock);
        if (!thread->flag) {
            thread->flag = true;
            thread->cond.notify_one();
        }
    }

    // Park wrappers
    static void park() {
        park_inner([](ThreadID me, std::unique_lock<std::mutex> &lk) {
                me->cond.wait(lk);
        });
    }

    template<class Rep, class Period>
    static void park_for(const std::chrono::duration<Rep, Period>& relTime) {
        park_inner([&](ThreadID me, std::unique_lock<std::mutex> &lk) {
                me->cond.wait_for(lk, relTime);
        });
    }

    template<class Clock, class Duration>
    static void park_until(
        const std::chrono::time_point<Clock, Duration>& timeout) {
        park_inner([&](ThreadID me, std::unique_lock<std::mutex> &lk) {
                me->cond.wait_until(lk, timeout);
        });
    }
};
}

#endif

#endif
