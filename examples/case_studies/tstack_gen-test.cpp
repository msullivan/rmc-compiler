#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include <thread>
#include <getopt.h>
#include <set>

#include "util.hpp"
#include "tstack_gen.hpp"

using namespace rmclib;

typedef unsigned long ulong;

const ulong kCount = 10000000;

struct Test {
    TStackGen<long> stack1;
    TStackGen<long> stack2;

    std::atomic<bool> producersDone{false};
    std::atomic<ulong> totalSum{0};
    std::atomic<ulong> totalCount{0};

    const int nodes{10000};
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

void worker(Test *t, TStackGen<long> &in, TStackGen<long> &out) {
    for (int i = 0; i < t->count; i++) {
        auto node = in.popNode();
        work();
        if (node) out.pushNode(node);
    }
}

void producer(Test *t) {
    worker(t, t->stack1, t->stack2);
}

void consumer(Test *t) {
    worker(t, t->stack2, t->stack1);
}

void setup(Test &t) {
    assert(t.nodes % 2 == 0);
    for (int i = 1; i <= t.nodes/2; i++) {
        t.stack1.pushNode(new TStackGen<long>::TStackNode(i));
        t.stack2.pushNode(new TStackGen<long>::TStackNode(-i));
    }
}

void check(Test &t) {
    int count = 0;
    // XXX: check values
    std::set<long> seen;

    auto checkStack = [&] (TStackGen<long> &stack) {
        TStackGen<long>::TStackNode *node;
        while ((node = stack.popNode())) {
            long val = node->data();
            if (seen.count(val)) {
                printf("Duplicate value: %ld\n", val);
                std::terminate();
            }
            seen.insert(val);
            count++;
        }
    };
    checkStack(t.stack1);
    checkStack(t.stack2);

    assert(count == t.nodes);
}

static bool verboseOutput = true;

void test(Test &t) {
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    setup(t);

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

    check(t);
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

void lol() {
}
