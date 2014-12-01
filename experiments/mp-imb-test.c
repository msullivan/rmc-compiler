// Test if message passing works
// Should on x86, not on arm
// Basically always just doesn't see the write at all.
// Probably need to loop.

// On ARM:
// mp        - observed
// mp+ctrl   - observed
// mp+addr   - not observed :( was hoping to
// mp+sync+addr - not observed; good!


#include "atomic.h"
#include <stdio.h>
#include <assert.h>

volatile long data PADDED = 1;
volatile long ready PADDED = 0;

volatile long *pdata;

volatile int foo;

int thread0()
{
    // Trying a bunch of things to see if I can get that ready write
    // to happen before the data one, but I haven't managed.
//    extern int go; /* durrrr */
//    data = go * go + 1;
    *pdata = 1;
//    smp_mb();
    ready = 1;
    return 0;
}

int thread1()
{
    int rready;
    while (!(rready = ready));
    ctrl_isync(rready);
    int rdata = data;
    return ((!!rdata)<<1) | rready;
}

void reset()
{
    pdata = &data;
    assert(data != 0);
    ready = 0;
    data = 0;
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
