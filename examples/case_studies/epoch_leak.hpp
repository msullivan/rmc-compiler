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
private:
public:
    Guard() { }
    ~Guard() { }
    template <typename F>
    void registerCleanup(F f) { }
    template <typename T>
    void unlinked(T *p) { }
};

class Epoch {
public:
    static Guard pin() {
        return Guard();
    }
};

}

#endif
