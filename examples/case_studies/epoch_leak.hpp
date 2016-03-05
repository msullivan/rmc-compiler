#ifndef RMC_EPOCH_LEAK
#define RMC_EPOCH_LEAK

#include <exception>

// A dummy version of the epoch lib that is just a total no-op.

namespace rmclib {
#if 0
} // f. this
#endif
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
    static void rcuSynchronize() { std::terminate(); }
};

}

#endif
