// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_EPOCH_LEAK
#define RMC_EPOCH_LEAK

#include <cstdio>
#include <cstdlib>

// A dummy version of the epoch lib that is just a total no-op.

namespace rmclib {
/////////////////////////////

template<class T> using lf_ptr = T*;

////////////////////////////

class GarbageCleanup {
private:
    typedef void (*Func)(void *);
    Func func_;
    void *data_;
public:
    GarbageCleanup(Func func, void *data) : func_(func), data_(data) {}
    void operator()() { func_(data_); }
};

class Guard {
private:
public:
    Guard() { }
    ~Guard() { }
    void registerCleanup(GarbageCleanup f) { }
    template <typename T>
    void unlinked(T *p) { }
    bool tryCollect() { return false; }
};

class Epoch {
public:
    static Guard pin() {
        return Guard();
    }
    static Guard rcuPin() {
        return Guard();
    }
    static void rcuSynchronize() {
        fprintf(stderr, "Can't rcuSynchronize() with epoch_leak!\n");
        exit(0); // exit success to troll the test harness...
    }
};

}

#endif
