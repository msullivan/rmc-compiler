#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <experimental/optional>
#include <atomic>
#include <thread>
#include <getopt.h>

#include "util.hpp"
#include "tstack.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

struct Test {
    rmclib::TStack<ulong> stack;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};
    std::atomic<ulong> totalCount{0};

    const long count;
    const int producers;
    const int consumers;
    Test(int c, int pr, int co) : count(c), producers(pr), consumers(co) {}
};

void work() {
    const int kWork = 50;
    volatile int nus = 0;
    for (int i = 0; i < kWork; i++) {
        nus++;
    }
}

void producer(Test *t) {
    for (int i = 1; i < t->count; i++) {
        work();
        t->stack.push(i);
    }
}

void consumer(Test *t) {
    ulong sum = 0;
    ulong count = 0; // lurr
    bool producersDone = false;
    for (;;) {
        auto res = t->stack.pop();
        work();
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

static bool verboseOutput = true;

void test(Test &t) {
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

    timer.report(t.count * (t.producers+t.consumers), verboseOutput);

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
        rmclib::joinAll(tests);
    }

    return 0;
}
