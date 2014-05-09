// Test if message passing works
// Should on x86, not on arm
// Basically always just doesn't see the write at all.
// Probably need to loop.

#include <atomic>
#include <stdio.h>

// Note that writing the test in C++ is kind of bogus, since
// the *compiler* can reorder.
std::atomic<long> data = {0};
std::atomic<long> ready = {0};

int thread0()
{
    data.store(1, std::memory_order_relaxed);
    ready.store(1, std::memory_order_relaxed);
    return 0;
}

int thread1()
{
    int rready = ready.load(std::memory_order_relaxed);
    int rdata = data.load(std::memory_order_relaxed);
    return (rdata<<1) | rready;
}

void reset()
{
    data.store(0, std::memory_order_relaxed);
    ready.store(0, std::memory_order_relaxed);
}

// Formatting results
int result_counts[4];

void process_results(int *r)
{
    int idx = r[1];
    result_counts[idx]++;
}

void summarize_results()
{
    for (int r0 = 0; r0 <= 1; r0++) {
        for (int r1 = 0; r1 <= 1; r1++) {
            int idx = (r1<<1) | r0;
            printf("ready=%d data=%d: %d\n", r0, r1, result_counts[idx]);
        }
    }
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1};

int thread_count = 2;
