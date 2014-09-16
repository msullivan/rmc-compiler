// Test for load buffering behavior
// Can two CPUs read from subsequent writes on the other one?

#include "atomic.h"
#include <stdio.h>

volatile long x PADDED = 0;
volatile long y PADDED = 0;

int thread0()
{
    int ry = y;
    x = 1;
    return ry;
}

int thread1()
{
    int rx = x;
    y = 1;
    return rx;
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
    if (idx == 3) printf("got one!\n");
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
