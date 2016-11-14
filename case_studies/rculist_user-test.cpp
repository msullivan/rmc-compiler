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

#include "rculist_user.hpp"
#include "epoch.hpp"

using namespace rmclib;

const ulong kCount = 10000000;
const ulong kInterval = 10000;
const ulong kElems = 100;


const int kWriteStride = 1039;
const int kReadStride = 3547;


struct Foo {
    int a;
    int b;
};

struct Test {
    nooblist noobs;

    alignas(kCacheLinePadding)
    std::atomic<bool> consumersDone{false};

    const long count;
    const int producers;
    const int consumers;
    const int elems;
    const int interval;
    Test(int c, int pr, int co, int el, int iv) :
        count(c), producers(pr), consumers(co), elems(el), interval(iv) {}
};


thread_local unsigned writeIdx = 0;
thread_local unsigned produceCnt = 0;

void produce(Test *t, int threadnum) {
    writeIdx += kWriteStride;
    unsigned key = writeIdx % t->elems;
    unsigned val1 = writeIdx * 100 + threadnum;
    unsigned val2 = val1 - key;
    noob *obj = new noob(key, val1, val2);

    noob_insert(&t->noobs, obj);
}

// Producers just produce but are off by default.
void producer(Test *t, int threadnum) {
    writeIdx = threadnum;
    while (!t->consumersDone) {
        produce(t, threadnum);
        busywait(0.1);
    }
}

#ifdef RCULIST_USER_RMC
void consume(Test *t, unsigned key) {
    XEDGE(find, a);
    auto guard = Epoch::rcuPin();

    noob *nobe = LTAKE(find, noob_find_give(&t->noobs, key));
    assert(nobe);
    assert_eq(key, L(a, nobe->val1) - L(a, nobe->val2));
}

#else
void consume(Test *t, unsigned key) {
    auto guard = Epoch::rcuPin();

    noob *nobe = noob_find(&t->noobs, key);
    assert(nobe);
    assert_eq(key, nobe->val1 - nobe->val2);
}
#endif

// Consumers mostly read the value but update it every t->interval
// iterations.
void consumer(Test *t, int threadnum) {
    // Try to stagger where in the cycle threads do their updates.
    int offset = (t->interval / t->consumers) * threadnum;
    unsigned readIdx = threadnum;

    for (int i = 1; i < t->count; i++) {
        if (t->interval && i % t->interval == offset) produce(t, threadnum);
        readIdx += kReadStride;
        consume(t, readIdx % t->elems);

        auto guard = Epoch::rcuPin();

    }
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void build_list(nooblist *noobs, int size) {
    for (int i = 0; i < size; i++) {
        noob *node = new noob(i, i, 0);
        noob_insert(noobs, node);
    }
}


void test(Test &t) {
    build_list(&t.noobs, 100);

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    BenchTimer timer;

    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t, i));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t, i));
    }

    joinAll(consumers);
    timer.stop();
    t.consumersDone = true;
    joinAll(producers);

    timer.report(t.count * t.consumers, !BenchMode);
}

cl::opt<int> Producers("p", cl::desc("Number of producer threads"),
                       cl::init(0));
cl::opt<int> Consumers("c", cl::desc("Number of consumer threads"),
                       cl::init(4));
cl::opt<int> Count("n", cl::desc("Number of per-thread operations"),
                   cl::init(kCount));
cl::opt<int> Reps("r", cl::desc("Number of times to repeat"), cl::init(1));
cl::opt<int> Dups("d", cl::desc("Number of times to duplicate"), cl::init(1));
cl::opt<int> Elems("e", cl::desc("Number of list elements"), cl::init(kElems));
cl::opt<int> Interval("i", cl::desc("Interval between writes"),
                      cl::init(kInterval));

int main(int argc, char** argv) {
    cl::ParseCommandLineOptions(argc, argv);

    for (int i = 0; i < Reps; i++) {
        std::vector<std::thread> tests;
        for (int j = 0; j < Dups; j++) {
            tests.push_back(std::thread([=] {
                Test t(Count, Producers, Consumers, Elems, Interval);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
