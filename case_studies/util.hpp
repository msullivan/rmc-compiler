// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMCLIB_UTIL
#define RMCLIB_UTIL

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <experimental/optional>
#include <assert.h>
#include <unistd.h>

#define rmc_noinline __attribute__((noinline))

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

// Abbreviations for the memory orders
const std::memory_order mo_rlx = std::memory_order_relaxed;
const std::memory_order mo_rel = std::memory_order_release;
const std::memory_order mo_acq = std::memory_order_acquire;
const std::memory_order mo_acq_rel = std::memory_order_acq_rel;
const std::memory_order mo_sc = std::memory_order_seq_cst;


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
    bool reported_{false};
    const char *name_;
public:
    void start() { start_ = std::chrono::high_resolution_clock::now(); }
    BenchTimer(const char *name = nullptr) : name_(name) { start(); }
    ~BenchTimer() { report(); }
    void stop() {
        if (!stopped_) {
            stop_ = std::chrono::high_resolution_clock::now();
            stopped_ = true;
        }
    }
    void report(long numOps = 0, bool verbose = true) {
        stop();
        if (reported_) return;
        reported_ = true;

        auto int_ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(stop_ - start_);

        auto int_ns = std::chrono::duration_cast<
            std::chrono::nanoseconds>(stop_ - start_);
        std::chrono::duration<double, std::nano> fp_ns = stop_ - start_;

        if (verbose) {
            if (name_) printf("%s: ", name_);
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

class CPUTracker {
    unsigned startCPU_;
    const char *name_;
public:
    CPUTracker(const char *name = "<anon>") : name_(name) {
        startCPU_ = sched_getcpu();
    }
    ~CPUTracker() {
        unsigned stop = sched_getcpu();
        printf("%s: Started on %u, finished on %u\n", name_, startCPU_, stop);
    }
};

static void joinAll(std::vector<std::thread> &threads) {
    for (auto & thread : threads) {
        thread.join();
    }
}

const int kDefaultWork = 50;
static void fakeWork(int work = kDefaultWork) {
    volatile int nus = 0;
    for (int i = 0; i < work; i++) {
        nus++;
    }
}

static size_t getEnvironSize() {
    size_t size = 0;
    for (char **ss = environ; *ss; ss++) {
        size += sizeof(*ss);
        size += strlen(*ss)+1;
    }
    return size;
}

}

#endif
