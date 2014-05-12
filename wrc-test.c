// Try wrc
// This never actually happens on my arm machine.

#include "atomic.h"
#include <stdio.h>


volatile long x PADDED = 1;
volatile long y PADDED = 1;

int thread0()
{
    x = 1;
    return 0;
}

int thread1()
{
    int rx;
    while (!(rx = x));
    y = 1;
    return 0;
}

int thread2()
{
    int ry;
    while (!(ry = y));
    int rx = x;
    return (rx<<1) | ry;
}


void reset()
{
    x = y = 0;
}

// Formatting results
int result_counts[4];

void process_results(int *r)
{
    int idx = r[2];
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
    printf("&x = %p, &y=%p\n", &x, &y);
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1, thread2};

int thread_count = 3;
