// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>

#include "util.hpp"
#include "seqlock.hpp"

#include "llvm-cl/CommandLine.h"

using namespace rmclib;

const ulong kCount = 1000000000;
const ulong kInterval = 10000;

struct Foo {
    std::atomic<int> a;
    std::atomic<int> b;
};

struct Test {
    SeqLock lock;

    alignas(kCacheLinePadding)
    Foo foo;

    alignas(kCacheLinePadding)
    std::atomic<bool> consumersDone{false};
    std::atomic<ulong> totalSum{0};

    const long count;
    const int producers;
    const int consumers;
    const int interval;
    Test(int c, int pr, int co, int iv)
        : count(c), producers(pr), consumers(co), interval(iv) {}
};

void produce(Test *t) {
    t->lock.write_lock();
    int i = t->foo.a + 1;
    t->foo.a = i;
    t->foo.b = -i;
    t->lock.write_unlock();
}

// Producers just produce but are off by default.
void producer(Test *t) {
    while (!t->consumersDone) {
        produce(t);
        busywait(0.1);
    }
}

// Consumers mostly read the value but update it every t->interval
// iterations.
void consumer(Test *t, int threadnum) {
    long max = 0;
    // Try to stagger where in the cycle threads do their updates.
    int offset = (t->interval / t->consumers) * threadnum;

    for (int i = 1; i < t->count; i++) {
        int a, b;
        for (;;) {
            auto tag = t->lock.read_lock();
            a = t->foo.a.load(mo_rlx);
            b = t->foo.b.load(mo_rlx);
            if (t->lock.read_unlock(tag)) break;
        }

        assert_eq(a, -b);
        if (a > max) max = a;

        if (t->interval && i % t->interval == offset) produce(t);
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

    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t, i));
    }

    joinAll(consumers);
    timer.stop();
    t.consumersDone = true;
    joinAll(producers);

    printf("Max thing: %ld\n", t.totalSum.load());

    timer.report(t.count * t.consumers, !BenchMode);
}

cl::opt<int> Producers("p", cl::desc("Number of producer threads"),
                       cl::init(0));
cl::opt<int> Consumers("c", cl::desc("Number of consumer threads"),
                       cl::init(2));
cl::opt<int> Count("n", cl::desc("Number of per-thread operations"),
                   cl::init(kCount));
cl::opt<int> Reps("r", cl::desc("Number of times to repeat"), cl::init(1));
cl::opt<int> Dups("d", cl::desc("Number of times to duplicate"), cl::init(1));

cl::opt<int> Interval("i", cl::desc("Interval between writes"),
                      cl::init(kInterval));


int main(int argc, char** argv) {
    cl::ParseCommandLineOptions(argc, argv);

    for (int i = 0; i < Reps; i++) {
        std::vector<std::thread> tests;
        for (int j = 0; j < Dups; j++) {
            tests.push_back(std::thread([=] {
                Test t(Count, Producers, Consumers, Interval);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
