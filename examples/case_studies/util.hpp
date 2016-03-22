#ifndef RMCLIB_UTIL
#define RMCLIB_UTIL

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <experimental/optional>
#include <assert.h>

#ifndef __ASSERT_FUNCTION
#define __ASSERT_FUNCTION NULL
#endif

#define assert_op(e1, op, e2) do {               \
        __typeof__(e1) _____t1 = (e1);          \
        __typeof__(e1) _____t2 = (e2);          \
        if (!(_____t1 op _____t2)) {            \
            fprintf(stderr,                                             \
                    "%s:%d: %s%sAssertion failed: %s (%ld) %s %s (%ld)\n", \
                    __FILE__, __LINE__,                                 \
                    __ASSERT_FUNCTION ? __ASSERT_FUNCTION : "",         \
                    __ASSERT_FUNCTION ? ": " : "",                      \
                    #e1, (long)_____t1, #op, #e2, (long)_____t2);       \
            abort();                                                    \
        }                                                               \
    } while (0)
#define assert_eq(e1, e2) assert_op(e1, ==, e2)
#define assert_ne(e1, e2) assert_op(e1, !=, e2)


namespace rmclib {

// Since optional might get moved out of experimental at some point,
// we put the include and using of it here so we can change it in one
// place if we need to.
using std::experimental::optional;

const int kCacheLinePadding = 64; // I have NFI

template<class T> using lf_ptr = T*;

// A proper C++-style implementation of container_of: given a
// pointer-to-member for some class and also an actual pointer to that
// member, recover a pointer to the whole object.
template<class C, typename T>
static inline C *container_of(T *ptr, T C::* member) {
    uintptr_t offset = reinterpret_cast<uintptr_t>(&((C *)(nullptr)->*member));
    return reinterpret_cast<C *>(reinterpret_cast<uintptr_t>(ptr) - offset);
}

static void busywait(double us) {
    auto start = std::chrono::high_resolution_clock::now();
    for (;;) {
        auto now = std::chrono::high_resolution_clock::now();
        // asdf, double
        std::chrono::duration<double, std::micro> elapsed = now-start;
        if (elapsed.count() > us) break;
    }
}

class BenchTimer {
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_, stop_;
    bool stopped_{false};
public:
    void start() { start_ = std::chrono::high_resolution_clock::now(); }
    BenchTimer() { start(); }
    void stop() {
        if (!stopped_) {
            stop_ = std::chrono::high_resolution_clock::now();
            stopped_ = true;
        }
    }
    void report(long numOps = 0, bool verbose = true) {
        stop();
        auto int_ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(stop_ - start_);

        auto int_ns = std::chrono::duration_cast<
            std::chrono::nanoseconds>(stop_ - start_);
        std::chrono::duration<double, std::nano> fp_ns = stop_ - start_;

        if (verbose) {
            printf("Runtime: %lldms\n", (long long)int_ms.count());
            if (numOps > 0) {

                printf("Time/op: %lldns (or %lfns)\n",
                       (long long)int_ns.count() / numOps,
                       fp_ns.count() / numOps);
            }
        } else {
            printf("%lld,%lf\n",
                   (long long)int_ms.count(),
                   fp_ns.count() / numOps);
        }
    }
};

static void joinAll(std::vector<std::thread> &threads) {
    for (auto & thread : threads) {
        thread.join();
    }
}

}

#endif
