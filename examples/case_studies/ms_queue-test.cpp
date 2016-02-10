#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <experimental/optional>
#include <getopt.h>

#include "ms_queue.hpp"

typedef unsigned long ulong;

static ulong kCount = 10000000;

struct Test {
    rmclib::MSQueue<ulong> queue;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};

    const long count;
    const int producers;
    const int consumers;
    Test(int c, int pr, int co) : count(c), producers(pr), consumers(co) {}
};

void producer(Test *t) {
    for (int i = 1; i < t->count; i++) {
        t->queue.enqueue(i);
    }
}

void consumer(Test *t) {
    ulong max = 0;
    ulong sum = 0;
    for (;;) {
        auto res = t->queue.dequeue();
        if (!res) {
            if (t->producersDone) break;
        } else {
            ulong val = *res;
            if (!(val < t->count)) {
                printf("owned %lu\n", val);
                abort();
            }
            assert(t->producers > 1 || val > max);
            max = val;
            sum += val;
        }
    }
    // Stupidly, this winds up being "more atomic" than using cout
    //printf("Done: %lu\n", sum);
    t->totalSum += sum;
}

void joinAll(std::vector<std::thread> &threads) {
    for (auto & thread : threads) {
        thread.join();
    }
}

void test(Test &t) {
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    for (int i = 0; i < t.producers; i++) {
        producers.push_back(std::thread(producer, &t));
    }
    for (int i = 0; i < t.consumers; i++) {
        consumers.push_back(std::thread(consumer, &t));
    }

    joinAll(producers);
    t.producersDone = true;
    joinAll(consumers);

    // This is real dumb, but overflow means we can't use the closed form...
    ulong expected = 0;
    for (int i = 0; i < t.count; i++) {
        expected += i;
    }
    expected *= t.producers;

    //printf("Final sum: %ld\n", t.totalSum.load());
    assert(t.totalSum == expected);
}

int main(int argc, char** argv) {
    int producers = 1, consumers = 1;
    long count = kCount;
    int reps = 1;
    int dups = 1;

    // getopt kind of ugly. I kind of want to use llvm's arg parsing,
    // which I <3, but it is such a big hammer
    int opt;
    while ((opt = getopt(argc, argv, "p:c:n:r:d:")) != -1) {
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
