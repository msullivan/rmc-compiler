// General mechanism for blocking threads. Based on the interface that
// Java and Rust use.

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

    // XXX: think about more; we should maybe have a better spec...
    // Is the behavior we want this?:
    // There is a consistent total order of park()/unpark() actions.
    // Any unpark() that is before a park() in that order should
    // happen-before it
    // park() happens as it is leaving, I guess...
    static void park() {
        ThreadID me = getCurrent();
        // Could do an if and have spurious wakeups but why?
        while (!me->flag) {
            futex(&me->flag, FUTEX_WAIT_PRIVATE, 0);
        }
        // Do the exchange so that we see all the writes we are overwriting
        // XXX: is there a better way??
        me->flag.exchange(0);
    }
    static void unpark(ThreadID thread) {
        // exchange keeps it all together in a release sequence
        // and lets us avoid signalling if it is already up
        if (thread->flag.exchange(1) != 1) {
            futex(&thread->flag, FUTEX_WAKE_PRIVATE, 1);
        }
    }
};

#elif USE_PTHREAD_PARKING

// This is a really basic implementation of the parking scheme
// But using pthread stuff directly
// The advantage here is that no constructor needs to run for the
// thread local nodes.
#include <pthread.h>

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

    static void park() {
        ThreadID me = getCurrent();
        pthread_mutex_lock(&me->lock);
        // if we do if, we could maybe spuriously wakeup
        while (!me->flag) {
            pthread_cond_wait(&me->cond, &me->lock);
        }
        me->flag = false;
        pthread_mutex_unlock(&me->lock);
    }
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
};

#else

// This is a really basic implementation of the parking scheme
// using C++ locks and cvars
#include <mutex>
#include <condition_variable>

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

    static void park() {
        ThreadID me = getCurrent();
        std::unique_lock<std::mutex> lk(me->lock);
        // if we do if, we could maybe spuriously wakeup
        while (!me->flag) {
            me->cond.wait(lk);
        }
        me->flag = false;
    }
    static void unpark(ThreadID thread) {
        std::unique_lock<std::mutex> lk(thread->lock);
        if (!thread->flag) {
            thread->flag = true;
            thread->cond.notify_one();
        }
    }
};
#endif
