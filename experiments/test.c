#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "atomic.h"

#define MAX_THREADS 8

// Things the test we link against should provide:

extern void process_results(int *r);
extern void summarize_results();
extern void reset();


typedef int (test_fn)();
extern test_fn *test_fns[];

extern int thread_count;

////////////////////////////

#define ITERATIONS 1000000


int go;
bool done[MAX_THREADS];
int results[MAX_THREADS];

void iteration(int thread, int *count) {
    test_fn *f = test_fns[thread];
    while (smp_load_acquire(&go) == *count)
        ;
    int r = f();
    results[thread] = r;
    smp_store_release(&done[thread], true);

    (*count)++;
}

void *tester_thread(void *pthread)
{
    int thread = (int)(long)pthread;
    int count = 0;
    while (1) {
        iteration(thread, &count);
    }
    return NULL;
}

// Argh. On dual-core machines it doesn't do very well to have one
// thread orchestrating the stuff in addition to the test threads. Be
// able to use one of the test threads to orchestrate.
#ifdef CORE_SHORTAGE

#define FIRST_THREAD 1
void run_test(int threads, int n)
{
    reset();
    // Let my people go
    smp_store_release(&go, n+1);

    // Run the test ourself
    iteration(0, &n);

    // Wait for everyone else
    for (int i = 0; i < threads; i++) {
        while (!smp_load_acquire(&done[i]))
            ;

        smp_store_release(&done[i], false);
    }

    // Everyone done, process results.
    process_results(results);
}

#else

#define FIRST_THREAD 0
void run_test(int threads, int n)
{
    reset();
    // Let my people go
    smp_store_release(&go, n+1);
    // Wait for everyone
    for (int i = 0; i < threads; i++) {
        while (!smp_load_acquire(&done[i]))
            ;

        smp_store_release(&done[i], false);
    }

    // Everyone done, process results.
    process_results(results);
}

#endif

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

    for (int i = FIRST_THREAD; i < thread_count; i++) {
        pthread_t thread;
        pthread_create(&thread, NULL, tester_thread, (void *)(long)i);
    }

    run_tests(thread_count, iterations);
    summarize_results();
}
