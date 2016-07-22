// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

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
