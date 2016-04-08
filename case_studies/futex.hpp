// Implementation of "futexes", a scheme for maintaining blocking
// queues associated with memory addresses. We can use actual Linux
// futexes or

// I think that futexes can be modeled as a stupidly high-level RMW
// operation. (Ignoring timeouts and synchronizing destruction...)
//
// We model it as something like:
//
// Futex = atomic<(i32, thread list)>
//
// futex_wait(f, i) = {
//     RMW[f, <i', ts>. if i = i' then <i', ts ++ [me]>
//                                else <i', ts>];
//     while me \in R[f].2: spin
// }
// futex_wake(f, n) =
//     RMW[f, <i', ts>. <i', drop n ts>]
//
// The idea here is that a futex is a thread queue and an integer.
// futex_wait atomically checks the integer matches and if so adds the
// current thread to the wait queue and then spins until it has been
// removed.
// futex_wake just removes threads from the queue.
//
// This model doesn't support doing non-RMW writes to the underlying
// value, though, which could be a problem.
//
// There would also probably be additional memory ordering constraints
// - probably rel-acq/vpre+xpost for both?

#ifndef RMC_FUTEX
#define RMC_FUTEX

#if defined(__linux__)
#define USE_REAL_FUTEX 1
#endif

#if USE_REAL_FUTEX

#include <cstdint>
#include <atomic>
#include <condition_variable>

#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "chrono_to_timespec.hpp"

namespace rmclib {

class Futex {
public:
    using Handle = uintptr_t;
private:
    static int
    futex(Handle handle, int futex_op, int val,
          const struct timespec *timeout = nullptr,
          int *uaddr2 = nullptr, int val3 = 0) {
        return syscall(SYS_futex,
                       handle, futex_op, val,
                       timeout, uaddr2, val3);
    }

    static const int kWaitOp = FUTEX_WAIT_PRIVATE;
    static const int kWaitAbsOp =
        FUTEX_WAIT_BITSET_PRIVATE|FUTEX_CLOCK_REALTIME;
    static const int kWakeOp = FUTEX_WAKE_PRIVATE;

    std::cv_status waitRet(int ret) {
        // I hate errno so much.
        return ret == -1 && errno == ETIMEDOUT ?
            std::cv_status::timeout : std::cv_status::no_timeout;
    }

public:
    std::atomic<int32_t> val;

    struct no_timeout_t {};
    static constexpr no_timeout_t no_timeout {};

    std::cv_status wait(int v, no_timeout_t nt = no_timeout) {
        return waitRet(futex(getHandle(), kWaitOp, v));
    }
    template<class Rep, class Period>
    std::cv_status wait(int v,
              const std::chrono::duration<Rep, Period>& relTime) {
        struct timespec ts = durationToTimespec(relTime);
        return waitRet(futex(getHandle(), kWaitOp, v, &ts));
    }
    template<class Clock, class Duration>
    std::cv_status wait(int v,
              const std::chrono::time_point<Clock, Duration>& timeout) {
        struct timespec ts = pointToTimeSpec(timeout);
        return waitRet(futex(getHandle(), kWaitAbsOp, v, &ts, nullptr, ~0));
    }

    // wake is split into two parts: getHandle returns a "handle" to
    // this futex and wake wakes up a handle. This is because, in a
    // lot of cases, we need to attempt wake()s on futexes that may
    // have just been destroyed!
    Handle getHandle() const { return reinterpret_cast<uintptr_t>(&val); }
    static void wake(Handle handle, int count) {
        futex(handle, kWakeOp, count);
    }

};

}

#else

#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace rmclib {

// This implementation is garbage in every way.
class Futex {
public:
    using Handle = uintptr_t;
    std::atomic<int32_t> val;

private:
    // The list structures
    struct Waiter {
        Handle handle;
        Waiter *next{nullptr}, *prev{nullptr};
        bool signaled{false};
        // For now we just use the list's lock
        //std::mutex lock;
        std::condition_variable cvar;

        void addBefore(Waiter *newElem) {
            Waiter *prev = this->prev;
            prev->next = newElem;
            this->prev = newElem;
            newElem->next = this;
            newElem->prev = prev;
        }
        void remove() {
            this->next->prev = this->prev;
            this->prev->next = this->next;
        }

        // For the dummy list heads
        Waiter() : handle(-1), next(this), prev(this) {}
        explicit Waiter(Handle h) : handle(h) {}
    };
    struct WaiterQueue {
        std::mutex lock;
        Waiter queue;
    };

    static const int kNumQueues = 127;
    static WaiterQueue queues_[kNumQueues];

    static WaiterQueue *getQueue(Handle h) {
        // XXX better hash
        return &queues_[(h >> 2) % kNumQueues];
    }

    template<typename F>
    std::cv_status waitInner(int v, F wait) {
        WaiterQueue *bucket = getQueue(getHandle());
        Waiter me(getHandle());
        std::unique_lock<std::mutex> lk(bucket->lock);
        if (val.load() != v) return std::cv_status::no_timeout;

        bucket->queue.addBefore(&me);
        // Should we handle spurious wakeup?
        // - Downside: wait_for may wait way too long
        auto ret = wait(me.cvar, lk);
        // If we *weren't* signaled, either because of a spurious
        // wakeup or a timeout, we need to remove ourselves from the
        // queue
        if (!me.signaled) {
            me.remove();
        }

        return ret;
    }

public:
    // wake is split into two parts: getHandle returns a "handle" to
    // this futex and wake wakes up a handle. This is because, in a
    // lot of cases (like when unlocking a mutex), we need to attempt
    // wake()s on futexes that may have just been destroyed!  This
    // annoying dynamic is the entire reason we need to have a
    // hashtable and queues and stuff instead of just having a
    // per-fake-futex mutex and cond var.
    Handle getHandle() const { return reinterpret_cast<uintptr_t>(&val); }
    static void wake(Handle handle, int count) {
        WaiterQueue *bucket = getQueue(handle);
        std::unique_lock<std::mutex> lk(bucket->lock);
        for (Waiter *nobe = bucket->queue.next; nobe != &bucket->queue;
             nobe = nobe->next) {

            if (nobe->handle == handle) {
                nobe->signaled = true;
                nobe->remove();
                nobe->cvar.notify_one();

                if (--count == 0) return;
            }
        }
    }


    //////////////////////////////////////
    // This is all boring nonsense.
    struct no_timeout_t {};
    static constexpr no_timeout_t no_timeout {};

    std::cv_status wait(int v, no_timeout_t nt = no_timeout) {
        return waitInner(v, [](auto &cv, auto &lk) {
                cv.wait(lk);
                return std::cv_status::no_timeout;
            });
    }

    template<class Rep, class Period>
    std::cv_status wait(int v,
              const std::chrono::duration<Rep, Period>& relTime) {
        return waitInner(v, [&](auto &cv, auto &lk) {
                return cv.wait_for(lk, relTime);
            });
    }
    template<class Clock, class Duration>
    std::cv_status wait(int v,
              const std::chrono::time_point<Clock, Duration>& timeout) {
        return waitInner(v, [&](auto &cv, auto &lk) {
                return cv.wait_until(lk, timeout);
            });
    }

};

}

#endif

#endif
