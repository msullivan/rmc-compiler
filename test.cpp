#include <thread>
#include <atomic>
#include <cassert>
#include <stdio.h>
#include <stdlib.h>

const int kMaxThreads = 8;

// Things the test we link against should provide:

extern void process_results(int *r);
extern void summarize_results();
extern void reset();


typedef int (test_fn)();
extern test_fn *test_fns[];

extern int thread_count;

////////////////////////////

#define ITERATIONS 1000000


std::atomic<int> go;
std::atomic<bool> done[kMaxThreads];
int results[kMaxThreads];

#define bs_acquire std::memory_order_acquire
#define bs_release std::memory_order_release
//#define bs_acquire std::memory_order_seq_cst
//#define bs_release std::memory_order_seq_cst

void tester_thread(int thread)
{
    test_fn *f = test_fns[thread];
    int count = 0;
    while (1) {
        while (go.load(bs_acquire) == count)
            ;
        int r = f();
        results[thread] = r;
        done[thread].store(true, bs_release);

        count++;
    }
}

void run_test(int threads, int n)
{
    reset();
    results[1] = 1;
    // Let my people go
    go.store(n+1, bs_release);
    // Wait for everyone
    for (int i = 0; i < threads; i++) {
        while (!done[i].load(bs_acquire))
            ;

        done[i].store(false, bs_release);
    }

    // Everyone done, process results.
    process_results(results);
}

void run_tests(int threads, int count)
{
    for (int i = 0; i < count; i++) {
        if (i && i % 10000000 == 0) printf("still truckin' %d\n", i);
        run_test(threads, i);
    }
}

int main(int argc, char **argv)
{
    int iterations = (argc == 2) ? atoi(argv[1]) : ITERATIONS;

    for (int i = 0; i < thread_count; i++) {
        std::thread t(tester_thread, i);
        t.detach();
    }

    run_tests(thread_count, iterations);
    summarize_results();
}
