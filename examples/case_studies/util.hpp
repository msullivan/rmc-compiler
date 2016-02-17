#ifndef RMCLIB_UTIL
#define RMCLIB_UTIL

#include <chrono>
#include <cstdio>

namespace rmclib {

const int kCacheLinePadding = 64; // I have NFI

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
    void report(long numOps = 0) {
        stop();
        auto int_ms = std::chrono::duration_cast<
            std::chrono::milliseconds>(stop_ - start_);
        printf("Runtime: %ldms\n", int_ms.count());
        if (numOps > 0) {
            auto int_ns = std::chrono::duration_cast<
                std::chrono::nanoseconds>(stop_ - start_);
            std::chrono::duration<double, std::nano> fp_ns = stop_ - start_;

            printf("Time/op: %ldns (or %lfns)\n",
                   int_ns.count() / numOps, fp_ns.count() / numOps);
        }
    }
};


}

#endif
