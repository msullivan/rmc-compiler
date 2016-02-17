// Test if message passing works
// Should on x86, not on arm
// Basically always just doesn't see the write at all.
// Probably need to loop.
// looking for: ready=1 data=0

#include "atomic.h"
#include <stdio.h>


volatile long data PADDED = 1;
volatile long ready PADDED = 1;

int thread0()
{
    data = 1;
    ready = 1;
    return 0;
}

int thread1()
{
    int rready;
    while (!(rready = ready));
    int rdata = data;
    return (rdata<<1) | rready;
}

void reset()
{
    data = ready = 0;
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
    printf("&data = %p, &ready=%p\n", &data, &ready);
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1};

int thread_count = 2;
