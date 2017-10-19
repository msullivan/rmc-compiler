// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include <thread>
#include "llvm-cl/CommandLine.h"

#include "util.hpp"
#include "ringbuf.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 100000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct CacheLineCounter {
    alignas(kCacheLinePadding)
    std::atomic<int> ctr{0};
};

#define USE_WIDE_OBJ 0
#if USE_WIDE_OBJ
struct Obj {
    ulong a_, b_;
    Obj() : a_(0), b_(0) {}
    Obj(unsigned char c) : a_(1337*c+20), b_(1338*c+20) {}
    operator unsigned long() { return b_ - a_; }
};
#else
using Obj = unsigned char;
#endif

const int kModulus = 251;

struct Test {
    Ringbuf<Obj> queue;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};
    std::atomic<ulong> totalCount{0};

    alignas(kCacheLinePadding)
    int fullCount{0};
    alignas(kCacheLinePadding)
    int emptyCount{0};

    const long count;

    Test(int c) : count(c) {}
};

void producer(Test *t) {
    int i = 0;
    while (i < t->count) {
        unsigned char c = i % kModulus;
        //fakeWork(Work);
        bool success = t->queue.enqueue(c);
        if (!success) {
            fakeWork(Work);
            t->fullCount++;
        } else {
            i++;
        }
    }
}

void consumer(Test *t) {
    //BenchTimer timer("consumer");
    //CPUTracker cpu("consumer");
    int i = 0;
    while (i < t->count) {
        auto res = t->queue.dequeue();
        //fakeWork(Work);
        if (!res) {
            fakeWork(Work);
            t->emptyCount++;
        } else {
            unsigned char expected = i % kModulus;
            ulong val = *res;
            assert_eq(val, expected);
            i++;
        }
    }
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    //CPUTracker cpu("tester");

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    rmclib::BenchTimer timer;

    std::thread producer_t(producer, &t);
    std::thread consumer_t(consumer, &t);
    producer_t.join();
    t.producersDone = true;
    consumer_t.join();

    //printf("empty = %d\n full = %d\n", t.emptyCount, t.fullCount);
    timer.report(t.count*2, !BenchMode);
}

cl::opt<int> Count("n", cl::desc("Number of per-thread operations"),
                   cl::init(kCount));
cl::opt<int> Reps("r", cl::desc("Number of times to repeat"), cl::init(1));
cl::opt<int> Dups("d", cl::desc("Number of times to duplicate"), cl::init(1));

int main(int argc, char** argv) {
    cl::ParseCommandLineOptions(argc, argv);

    for (int i = 0; i < Reps; i++) {
        std::vector<std::thread> tests;
        for (int j = 0; j < Dups; j++) {
            tests.push_back(std::thread([=] {
                Test t(Count);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
