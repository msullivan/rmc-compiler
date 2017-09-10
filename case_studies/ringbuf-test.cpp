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

const ulong kCount = 10000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct CacheLineCounter {
    alignas(kCacheLinePadding)
    std::atomic<int> ctr{0};
};

const int kModulus = 251;

struct Test {
    Ringbuf<unsigned char> queue;

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
