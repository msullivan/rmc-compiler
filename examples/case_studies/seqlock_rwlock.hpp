#ifndef SEQLOCK_RWLOCK_H
#define SEQLOCK_RWLOCK_H

#include <shared_mutex>
#include <utility>
#include "util.hpp"

namespace rmclib {

// This seems to really suck hard. On my system it does worse than the
// lock version with 7 readers!

class SeqLock {
public:
    using Tag = uintptr_t;

private:
    std::shared_timed_mutex lock_;

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
