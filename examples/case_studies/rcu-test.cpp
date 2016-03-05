// Test using epochs like RCU

#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <getopt.h>
#include <atomic>

#include "util.hpp"

#include "epoch.hpp"
#include "rcu_synchronize.hpp"

using namespace rmclib;

const ulong kCount = 1000000000;

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

const std::memory_order mo_rlx = std::memory_order_relaxed;
const std::memory_order mo_rel = std::memory_order_release;
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
        rcuSynchronize();

        fooIdx = !fooIdx;
        i++;

        busywait(0.1);
    }

}

void consumer(Test *t) {
    long max = 0;
    for (int i = 1; i < t->count; i++) {
        auto guard = Epoch::pinQuick();

        Foo *foo = fake_consume(t->foo);
        if (!foo) continue;

        int a = foo->a;
        int b = foo->b;

        assert_eq(a, -b);
        if (a > max) max = a;
    }

    t->totalSum += max;
}

void joinAll(std::vector<std::thread> &threads) {
    for (auto & thread : threads) {
        thread.join();
    }
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
        consumers.push_back(std::thread(consumer, &t));
    }

    joinAll(consumers);
    timer.stop();
    t.consumersDone = true;
    joinAll(producers);

    printf("Max thing: %ld\n", t.totalSum.load());

    timer.report(t.count * t.consumers, verboseOutput);
}

int main(int argc, char** argv) {
    int producers = 1, consumers = 1;
    long count = kCount;
    int reps = 1;
    int dups = 1;

    // getopt kind of ugly. I kind of want to use llvm's arg parsing,
    // which I <3, but it is such a big hammer
    int opt;
    while ((opt = getopt(argc, argv, "p:c:n:r:d:b")) != -1) {
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
                Test t(count, producers, consumers);
                test(t);
            }));
        }
        joinAll(tests);
    }

    return 0;
}
