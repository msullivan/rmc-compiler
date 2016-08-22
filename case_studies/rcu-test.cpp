// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// Test using epochs like RCU

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include "llvm-cl/CommandLine.h"

#include "util.hpp"

#include "epoch.hpp"

using namespace rmclib;

const ulong kCount = 500000000;

struct Foo {
    int a;
    int b;
};

struct Test {
    std::atomic<Foo *> foo{nullptr};

    alignas(kCacheLinePadding)
    std::atomic<bool> consumersDone{false};
    std::atomic<ulong> totalSum{0};

    const long count;
    const int producers;
    const int consumers;
    Test(int c, int pr, int co) : count(c), producers(pr), consumers(co) {}
};

template <typename T>
T fake_consume(std::atomic<T> &val) {
    // XXX: ALPHA or some shit also compilers wtvr
    return val.load(mo_rlx);
}


void work() {
    const int kWork = 500;
    volatile int nus = 0;
    for (int i = 0; i < kWork; i++) {
        nus++;
    }
}

void producer(Test *t) {
    Foo foos[2];
    int fooIdx = 0;

    int i = 1;
    while (!t->consumersDone) {
        Foo *foo = &foos[fooIdx];

        foo->a = i;
        work(); // lol
        foo->b = -i;

        t->foo.store(foo, mo_rel);
        Epoch::rcuSynchronize();

        fooIdx = !fooIdx;
        i++;

        busywait(0.1);
    }

}

void consumer(Test *t) {
    long max = 0;
    for (int i = 1; i < t->count; i++) {
        auto guard = Epoch::rcuPin();

        Foo *foo = fake_consume(t->foo);
        if (!foo) continue;

        int a = foo->a;
        int b = foo->b;

        assert_eq(a, -b);
        if (a > max) max = a;
    }

    t->totalSum += max;
}

cl::opt<int> BenchMode("b", cl::desc("Use benchmark output"));

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
        consumers.push_back(std::thread(consumer, &t));
    }

    joinAll(consumers);
    timer.stop();
    t.consumersDone = true;
    joinAll(producers);

    printf("Max thing: %ld\n", t.totalSum.load());

    timer.report(t.count * t.consumers, !BenchMode);
}

cl::opt<int> Producers("p", cl::desc("Number of producer threads"),
                       cl::init(1));
cl::opt<int> Consumers("c", cl::desc("Number of consumer threads"),
                       cl::init(1));
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
                Test t(Count, Producers, Consumers);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
