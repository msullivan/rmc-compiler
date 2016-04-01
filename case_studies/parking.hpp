// General mechanism for blocking threads. Based on the interface that
// Java and Rust use.

#define USE_PTHREAD_PARKING 1

#if USE_PTHREAD_PARKING

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
