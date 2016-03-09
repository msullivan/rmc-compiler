#ifndef SEQLOCK_NOOP_H
#define SEQLOCK_NOOP_H

namespace rmclib {

class SeqLock {
private:

public:
    using Tag = int;

    Tag read_lock() { return 0; }
    bool read_unlock(Tag tag) { return true; }
    void write_lock() { }
    void write_unlock() { }
};


}

#endif
