// Test if message passing works
// Should on x86, not on arm
// Basically always just doesn't see the write at all.
// Probably need to loop.

// On ARM:
// mp        - observed
// mp+ctrl   - observed
// mp+addr   - observed, but not often at all!
// mp+sync+addr - not observed; good!


#include "atomic.h"
#include <stdio.h>
#include <assert.h>



// Note that writing the test in C++ is kind of bogus, since
// the *compiler* can reorder.
volatile long data = 1;
volatile long ready = 0;

int thread0()
{
    data = 1;
//    smp_mb();
    ready = 1;
    return 0;
}

int thread1()
{
    int rready;
    //rready = ready;
    while (!(rready = ready));
    //int rdata = *bullshit_dep(&data, rready);
    int rdata = data;
    return (rdata<<1) | rready;
}

void reset()
{
    assert(data == 1);
    ready = 0;
    data = 0;
}

// Formatting results
int result_counts[4];

void process_results(int *r)
{
    int idx = r[1];
    if (idx != 3) printf("got one!\n");
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
