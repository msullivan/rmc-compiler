#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <experimental/optional>

#include "ms_queue.hpp"

long kCount = 10000000;

rmclib::MSQueue<int> queue;
std::atomic<bool> producersDone{false};

void producer(long count) {
    for (int i = 0; i < count; i++) {
        queue.enqueue(i);
    }
}

std::atomic<long> totalSum{0};
void consumer() {
    long sum = 0;
    for (;;) {
        auto res = queue.dequeue();
        if (!res) {
            if (producersDone) break;
        } else {
            sum += *res;
        }
    }
    // Stupidly, this winds up being "more atomic" than using cout
    printf("Done: %ld\n", sum);
    totalSum += sum;
}

void test(int nproducers, int nconsumers, long count) {
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    for (int i = 0; i < nproducers; i++) {
        producers.push_back(std::thread(producer, count));
    }
    for (int i = 0; i < nconsumers; i++) {
        consumers.push_back(std::thread(consumer));
    }

    for (auto & thread : producers) {
        thread.join();
    }
    producersDone = true;
    for (auto & thread : consumers) {
        thread.join();
    }
    printf("Final sum: %ld\n", totalSum.load());
    long expected = count*(count-1)/2 * nproducers;
    assert(totalSum == expected);
}

int main(int argc, char** argv) {
    int producers = 1, consumers = 1;
    long count = kCount;
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
    test(producers, consumers, count);

    return 0;
}
