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
#include "ms_queue.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

cl::opt<int> Work("w", cl::desc("Number of dummy work iterations"),
                  cl::init(kDefaultWork));

struct CacheLineCounter {
    alignas(kCacheLinePadding)
    std::atomic<int> ctr{0};
};

const int kMax = 128;

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
    const int trolls;

    CacheLineCounter ctrs[kMax];

    Test(int c, int pr, int co, int t) :
        count(c), producers(pr), consumers(co), trolls(t) {}
};

int guessSize(Test *t) {
    int count = 0;
    for (int i = 0; i < t->producers; i++) {
        count += t->ctrs[i].ctr.load(mo_rlx);
    }
    for (int i = t->producers; i < t->producers+t->consumers; i++) {
        count -= t->ctrs[i].ctr.load(mo_rlx);
    }
    return count;
}

const int backPressureCheckFreq = 8192/2;
// Backpressure at 100k does not seem to save us at /all/ but
// backpressure at 10k pretty often does.
const int backPressureMax = 10000;

void producer(Test *t, int no) {
    int maxSeen = 0;
    //BenchTimer timer("producer");
    //CPUTracker cpu("producer");
    std::atomic<int> &ctr = t->ctrs[no].ctr;

    for (int i = 1; i < t->count; i++) {
        //BenchTimer b;
        fakeWork(Work);
        //fakeWork(Work + Work/4);
        t->queue.enqueue(i);
        ctr.store(i, mo_rlx);
        /*
        int enqs = t->approxEnqueues.fetch_add(1, mo_rlx);
        int deqs = t->approxDequeues.load(mo_rlx);
        int size = enqs - deqs;
        if (size > maxSeen) maxSeen = size;
        */
        if (i % backPressureCheckFreq == 0) {
            int size = guessSize(t);
            if (size > maxSeen) maxSeen = size;
            while (size > backPressureMax) {
                fakeWork(Work);
                size = guessSize(t);
            }
        }

        //b.stop(true);
    }
    //printf("Max size: %d\n", maxSeen);
}

void consumer(Test *t, int no) {
    //BenchTimer timer("consumer");
    //CPUTracker cpu("consumer");
    std::atomic<int> &ctr = t->ctrs[no + t->producers].ctr;

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
            ctr.store(count+1, mo_rlx);
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
    //fprintf(stderr, "Done: %lu\n", sum);
    //fprintf(stderr, "Missed: %lu\n", missed);
    t->totalSum += sum;
    t->totalCount += count;
}

void troll(Test *t, int no) {
    ulong missed = 0;
    for (int i = 1; i < t->count; i++) {
        fakeWork(Work);
        t->queue.enqueue(i);
        fakeWork(Work);
        auto res = t->queue.dequeue();
        // XXX do something with res?
        if (!res) {
            missed++;
        }
    }
}

cl::opt<bool> BenchMode("b", cl::desc("Use benchmark output"));

void test(Test &t) {
    //CPUTracker cpu("tester");

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::vector<std::thread> trolls;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    rmclib::BenchTimer timer;

    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t, i));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t, i));
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
    //printf("env size: %zu\n", getEnvironSize());

    cl::ParseCommandLineOptions(argc, argv);

    if (Producers+Consumers > kMax) {
        printf("Too many producers/consumers: max = %d\n", kMax);
        return 1;
    }

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
