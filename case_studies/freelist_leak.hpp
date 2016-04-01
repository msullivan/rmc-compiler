#ifndef RMC_FREELIST_LEAK
#define RMC_FREELIST_LEAK

namespace rmclib {
/////////////////////////////

// A freelist that just leaks memory instead

template<typename T>
class Freelist {
public:
    // Allocate a new node. If we need to actually create a new one,
    // it will be constructed with the default constructor. And this
    // version never reuses stuff, so...
    T *alloc() { return new T(); }
    void unlinked(T *ptr) { }
};


//////

}

#endif
