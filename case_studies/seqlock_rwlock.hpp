#ifndef SEQLOCK_RWLOCK_H
#define SEQLOCK_RWLOCK_H

#include <shared_mutex>
#include <utility>
#include "rwlocks.hpp"
#include "util.hpp"

namespace rmclib {

class SeqLock {
public:
    using Tag = uintptr_t;

private:
    //std::shared_timed_mutex lock_;
    busy_rwlock_readstarve lock_;

public:
    Tag read_lock() {
        lock_.lock_shared();
        return 0;
    }

    bool read_unlock(Tag tag) {
        lock_.unlock_shared();
        return true;
    }

    void write_lock() { lock_.lock(); }
    void write_unlock() { lock_.unlock(); }
};


}

#endif
