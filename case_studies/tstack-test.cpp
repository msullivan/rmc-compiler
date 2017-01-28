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
#include "tstack.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct Test {
    rmclib::TStack<ulong> stack;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};
    std::atomic<ulong> totalCount{0};

    const long count;
    const int producers;
    const int consumers;
    const int trolls;
    Test(int c, int pr, int co, int t) :
        count(c), producers(pr), consumers(co), trolls(t) {}
};

void producer(Test *t) {
    for (int i = 1; i < t->count; i++) {
        fakeWork(Work);
        t->stack.push(i);
    }
}

void consumer(Test *t) {
    ulong sum = 0;
    ulong count = 0; // lurr
    bool producersDone = false;
    for (;;) {
        auto res = t->stack.pop();
        fakeWork(Work);
        if (!res) {
            // Bail once we fail to do a pop /after/ having
            // observered that the producers are done.  Otherwise we
            // could fail, the producer pushes a bunch more,
            // finishes, and then we exit without popping it.
            if (producersDone) break;
            producersDone = t->producersDone;
        } else {
            ulong val = *res;
            sum += val;
            count++;
        }
    }
    // Stupidly, this winds up being "more atomic" than using cout
    //printf("Done: %lu\n", sum);
    t->totalSum += sum;
    t->totalCount += count;
}

void troll(Test *t, int no) {
    ulong missed = 0;
    for (int i = 1; i < t->count; i++) {
        fakeWork(Work);
        t->stack.push(i);
        fakeWork(Work);
        auto res = t->stack.pop();
        // XXX do something with res?
        if (!res) {
            missed++;
        }
    }
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::vector<std::thread> trolls;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    rmclib::BenchTimer timer;

    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t));
    }
    for (int i = 0; i < t.trolls; i++) {
        trolls.push_back(std::thread(troll, &t, i));
    }

    joinAll(producers);
    t.producersDone = true;
    joinAll(consumers);
    joinAll(trolls);

    timer.report(t.count * (t.producers+t.consumers+t.trolls*2), !BenchMode);

    // This is real dumb, but overflow means we can't use the closed form...
    ulong expected = 0;
    for (int i = 0; i < t.count; i++) {
        expected += i;
    }
    expected *= t.producers;

    //printf("Final sum: %ld\n", t.totalSum.load());
    assert_eq(t.totalCount.load(), t.producers*(t.count-1));
    assert_eq(t.totalSum.load(), expected);
}

cl::opt<int> Producers("p", cl::desc("Number of producer threads"),
                       cl::init(1));
cl::opt<int> Consumers("c", cl::desc("Number of consumer threads"),
                       cl::init(1));
cl::opt<int> Trolls("t", cl::desc("Number of troll threads"),
                    cl::init(0));
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
                Test t(Count, Producers, Consumers, Trolls);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
