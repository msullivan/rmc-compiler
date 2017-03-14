// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>

#include "util.hpp"
#include "four_slot.hpp"

#include "llvm-cl/CommandLine.h"

using namespace rmclib;

const ulong kCount = 100000000;

// this test is not super great I think

struct Test {
    FourSlotSync<std::pair<int, int>> sync;

    alignas(kCacheLinePadding)
    std::atomic<ulong> totalSum{0};

    const long count;
    Test(int c)
        : count(c) {}
};

void producer(Test *t) {
    for (int i = 1; i < t->count; i++) {
        auto foo = std::make_pair(i, -i);
        t->sync.write(std::move(foo));
    }

}

void consumer(Test *t) {
    long max = 0;
    for (int i = 1; i < t->count; i++) {
        std::pair<int, int> foo = t->sync.read();

        int a = foo.first;
        int b = foo.second;

        assert_eq(a, -b);
        assert(a >= max);
        if (a > max) max = a;
    }

    t->totalSum += max;
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    BenchTimer timer;

    std::thread p = std::thread(producer, &t);
    std::thread c = std::thread(consumer, &t);

    c.join();
    p.join();
    timer.stop();

    //printf("Max thing: %ld\n", t.totalSum.load());

    timer.report(2 * t.count, !BenchMode);
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
