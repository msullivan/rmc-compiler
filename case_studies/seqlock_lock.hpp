#ifndef SEQLOCK_LOCK_H
#define SEQLOCK_LOCK_H

#include <mutex>
#include <utility>
#include "util.hpp"
#include "qspinlock.hpp"

namespace rmclib {

class SeqLock {
public:
    using Tag = uintptr_t;

private:
    //std::mutex lock_;
    QSpinLock lock_;

public:
    Tag read_lock() {
        lock_.lock();
        return 0;
    }

    bool read_unlock(Tag tag) {
        lock_.unlock();
        return true;
    }

    void write_lock() { lock_.lock(); }
    void write_unlock() { lock_.unlock(); }
};


}

#endif
