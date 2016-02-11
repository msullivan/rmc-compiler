#ifndef RMC_EPOCH_LEAK
#define RMC_EPOCH_LEAK

// A dummy version of the epoch lib that is just a total no-op.

namespace rmclib {
#if 0
} // f. this
#endif
/////////////////////////////

template<class T> using lf_ptr = T*;

////////////////////////////

class Guard {
public:
    ~Guard() {}
};

class Epoch {
public:
    static void enter() {  }
    static void exit() {  }
    template <typename F>
    static void registerCleanup(F f) { }
    template <typename T>
    static void unlinked(T *p) { }

    static Guard pin() {
        return Guard();
    }
};

}

#endif
