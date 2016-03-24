#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <getopt.h>
#include <atomic>

#include "util.hpp"
#include "seqlock.hpp"

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

const std::memory_order mo_rlx = std::memory_order_relaxed;

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

static bool verboseOutput = true;

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

    timer.report(t.count * t.consumers, verboseOutput);
}

int main(int argc, char** argv) {
    int producers = 0, consumers = 2;
    long count = kCount;
    int reps = 1;
    int dups = 1;
    int interval = kInterval;

    // getopt kind of ugly. I kind of want to use llvm's arg parsing,
    // which I <3, but it is such a big hammer
    int opt;
    while ((opt = getopt(argc, argv, "p:c:n:r:d:i:b")) != -1) {
        switch (opt) {
        case 'p':
            producers = atoi(optarg);
            break;
        case 'c':
            consumers = atoi(optarg);
            break;
        case 'n':
            count = strtol(optarg, NULL, 0);
            break;
        case 'r':
            reps = atoi(optarg);
            break;
        case 'd':
            dups = atoi(optarg);
            break;
        case 'i':
            interval = atoi(optarg);
            break;
        case 'b':
            verboseOutput = false;
            break;
        default:
            fprintf(stderr, "Argument parsing error\n");
            return 1;
        }
    }

    for (int i = 0; i < reps; i++) {
        std::vector<std::thread> tests;
        for (int j = 0; j < dups; j++) {
            tests.push_back(std::thread([=] {
                Test t(count, producers, consumers, interval);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
