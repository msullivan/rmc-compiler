// Test for store buffering behavior

#include <atomic>
#include <cstdio>

// Note that writing the test in C++ is kind of bogus, since
// the *compiler* can reorder.
std::atomic<long> x = {0};
std::atomic<long> y = {0};

int thread0()
{
    x.store(1, std::memory_order_relaxed);
    return y.load(std::memory_order_relaxed);
}

int thread1()
{
    y.store(1, std::memory_order_relaxed);
    return x.load(std::memory_order_relaxed);
}

void reset()
{
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
}

// Formatting results
int result_counts[4];

void process_results(int *r)
{
    int idx = (r[1]<<1) | r[0];
    result_counts[idx]++;
}

void summarize_results()
{
    for (int r0 = 0; r0 <= 1; r0++) {
        for (int r1 = 0; r1 <= 1; r1++) {
            int idx = (r1<<1) | r0;
            printf("r0=%d r1=%d: %d\n", r0, r1, result_counts[idx]);
        }
    }
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1};

int thread_count = 2;
