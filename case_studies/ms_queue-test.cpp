// Copyright (c) 2014-2016 Michael J. Sullivan
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
#include "ms_queue.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct Test {
    MSQueue<ulong> queue;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};
    std::atomic<ulong> totalCount{0};

    //alignas(kCacheLinePadding)
    //std::atomic<int> approxEnqueues{0};
    //alignas(kCacheLinePadding)
    //std::atomic<int> approxDequeues{0};

    const long count;
    const int producers;
    const int consumers;
    Test(int c, int pr, int co) : count(c), producers(pr), consumers(co) {}
};

void producer(Test *t) {
    //int maxSeen = 0;
    //BenchTimer timer("producer");
    //CPUTracker cpu("producer");
    for (int i = 1; i < t->count; i++) {
        //BenchTimer b;
        //fakeWork(Work);
        fakeWork(Work + Work/4);
        t->queue.enqueue(i);
        /*
        int enqs = t->approxEnqueues.fetch_add(1, mo_rlx);
        int deqs = t->approxDequeues.load(mo_rlx);
        int size = enqs - deqs;
        if (size > maxSeen) maxSeen = size;
        */
        //b.stop(true);
    }
    //printf("Max size: %d\n", maxSeen);
}

void consumer(Test *t) {
    //BenchTimer timer("consumer");
    //CPUTracker cpu("consumer");

    ulong missed = 0;
    ulong max = 0;
    ulong sum = 0;
    ulong count = 0; // lurr
    bool producersDone = false;
    for (;;) {
        auto res = t->queue.dequeue();
        fakeWork(Work);
        if (!res) {
            // Bail once we fail to do a dequeue /after/ having
            // observered that the producers are done.  Otherwise we
            // could fail, the producer enqueues a bunch more,
            // finishes, and then we exit without dequeueing it.
            if (producersDone) break;
            producersDone = t->producersDone;
            missed++;
        } else {
            //t->approxDequeues.fetch_add(1, mo_rlx);
            ulong val = *res;
            assert_op(val, <, t->count);
            if (t->producers == 1) {
                assert_op(val, >, max);
                if (t->consumers == 1) {
                    assert_eq(val, max+1);
                }
            }
            max = val;
            sum += val;
            count++;
        }
    }
    // Stupidly, this winds up being "more atomic" than using cout
    //printf("Done: %lu\n", sum);
    //printf("Missed: %lu\n", missed);
    t->totalSum += sum;
    t->totalCount += count;
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    //CPUTracker cpu("tester");

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    rmclib::BenchTimer timer;

    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t));
    }

    joinAll(producers);
    t.producersDone = true;
    joinAll(consumers);

    timer.report(t.count * (t.producers+t.consumers), !BenchMode);

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
cl::opt<int> Count("n", cl::desc("Number of per-thread operations"),
                   cl::init(kCount));
cl::opt<int> Reps("r", cl::desc("Number of times to repeat"), cl::init(1));
cl::opt<int> Dups("d", cl::desc("Number of times to duplicate"), cl::init(1));

int main(int argc, char** argv) {
    //printf("env size: %zu\n", getEnvironSize());

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
