#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <experimental/optional>

#include "ms_queue.hpp"

static long kCount = 10000000;

struct Test {
    rmclib::MSQueue<long> queue;

    std::atomic<bool> producersDone{false};
    std::atomic<long> totalSum{0};

    const long count;
    const int producers;
    const int consumers;
    Test(int c, int pr, int co) : count(c), producers(pr), consumers(co) {}
};

void producer(Test *t) {
    for (int i = 0; i < t->count; i++) {
        t->queue.enqueue(i);
    }
}

void consumer(Test *t) {
    long max = -1;
    long sum = 0;
    for (;;) {
        auto res = t->queue.dequeue();
        if (!res) {
            if (t->producersDone) break;
        } else {
            long val = *res;
            if (!(val >= 0 && val < t->count)) {
                printf("owned %lu\n", val);
                abort();
            }
            assert(t->producers > 1 || val > max);
            max = val;
            sum += val;
        }
    }
    // Stupidly, this winds up being "more atomic" than using cout
    //printf("Done: %ld\n", sum);
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

    //printf("Final sum: %ld\n", t.totalSum.load());
    long expected = t.count*(t.count-1)/2 * t.producers;
    assert(t.totalSum == expected);
}

int main(int argc, char** argv) {
    int producers = 1, consumers = 1;
    long count = kCount;
    int reps = 1;
    int dups = 1;

    // XXX: use an arg parsing library or something
    // (I really really <3 llvm's, but...)
    if (argc == 2) {
        int total = atoi(argv[1]);
        producers = total/2;
        consumers = total - producers;
    } if (argc >= 3) {
        producers = atoi(argv[1]);
        consumers = atoi(argv[2]);
    }
    if (argc >= 4) {
        count = strtol(argv[3], NULL, 0);
    }
    if (argc >= 5) {
        reps = atoi(argv[4]);
    }
    if (argc >= 6) {
        dups = atoi(argv[5]);
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
