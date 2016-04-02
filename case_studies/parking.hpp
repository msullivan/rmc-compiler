// General mechanism for blocking threads. Based on the interface that
// Java and Rust use.

#include <chrono>

#if defined(__linux__)
#define USE_FUTEX_PARKING 1
#elif defined(__unix__)
#define USE_PTHREAD_PARKING 1
#endif


#if USE_FUTEX_PARKING || USE_PTHREAD_PARKING
template<class Rep, class Period>
static inline struct timespec durationToTimespec(
    const std::chrono::duration<Rep, Period>& rel_time) {

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        rel_time).count();
    timespec ts = { .tv_sec = ns / 1000000000, .tv_nsec = ns % 1000000000 };
    return ts;
}

// We only support one clock right now because wtf
template<class Duration>
static inline struct timespec pointToTimeSpec(
    const std::chrono::time_point<
        std::chrono::system_clock, Duration>& timeout) {
        return durationToTimespec(timeout.time_since_epoch());
}

#endif


#if USE_FUTEX_PARKING

#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

namespace rmclib {

class Parking {
private:
    struct ThreadNode {
        std::atomic<int> flag;
    };
    static thread_local ThreadNode me_;


    static int
    futex(std::atomic<int> *uaddr, int futex_op, int val,
          const struct timespec *timeout = nullptr,
          int *uaddr2 = nullptr, int val3 = 0) {
        return syscall(SYS_futex,
                       reinterpret_cast<int*>(uaddr), futex_op, val,
                       timeout, uaddr2, val3);
    }

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
    static inline void park_inner(struct timespec *timeout, bool absTime) {
        // Are we waiting for an absolute or relative amount of time?
        auto op = absTime ?
            FUTEX_WAIT_BITSET_PRIVATE|FUTEX_CLOCK_REALTIME :
            FUTEX_WAIT;

        ThreadID me = getCurrent();
        // Doing an if allows spurious wakeups but means we don't need
        // any extra logic to handle timeouts.
        if (!me->flag) {
            futex(&me->flag, op, 0, timeout, nullptr, ~0);
        }
        // Do the exchange so that we see all the writes we are overwriting
        // XXX: is there a better way??
        me->flag.exchange(0);
    }
public:

    static void unpark(ThreadID thread) {
        // exchange keeps it all together in a release sequence
        // and lets us avoid signalling if it is already up
        if (thread->flag.exchange(1) != 1) {
            futex(&thread->flag, FUTEX_WAKE_PRIVATE, 1);
        }
    }

    // Park wrappers
    static void park() { park_inner(nullptr, false); }

    template<class Rep, class Period>
    static void park_for(const std::chrono::duration<Rep, Period>& relTime) {
        struct timespec ts = durationToTimespec(relTime);
        park_inner(&ts, false);
    }

    template<class Clock, class Duration>
    static void park_until(
        const std::chrono::time_point<Clock, Duration>& timeout) {
        struct timespec ts = pointToTimeSpec(timeout);
        park_inner(&ts, true);
    }
};

}

#elif USE_PTHREAD_PARKING

// This is a really basic implementation of the parking scheme
// But using pthread stuff directly
// The advantage here is that no constructor needs to run for the
// thread local nodes.
#include <pthread.h>

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
