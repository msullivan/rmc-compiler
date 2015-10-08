// Test whether storing to and then loading from a temporary in between
// the store and the load in a store buffering test prevents the
// store buffering behavior.
// (It doesn't)

#include "atomic.h"
#include <stdio.h>

volatile long x = 0;
volatile long y = 0;

volatile long tmp0;
volatile long tmp1;

int thread0()
{
    x = 1;
    tmp0 = 0; tmp0; // store/load pair
    return y;
}

int thread1()
{
    y = 1;
    tmp1 = 0; tmp1; // store/load pair
    return x;
}

void reset()
{
    x = y = 0;
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
