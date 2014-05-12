// Try wrc
// This works

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
    // Can't observe this with a ctrl dep here! Make sense since writes
    // won't get speculated before control dependent reads!
    //while (!(rx = x));
    rx = x;
    y = 1;
    return rx;
}

int thread2()
{
    int ry;
    // I manage to observe this with a ctrl dep, but not without one!
    //ry = y;
    while (!(ry = y));
    int rx = x;
    return (rx<<1) | ry;
}


void reset()
{
    x = y = 0;
}

// Formatting results
int result_counts[8];

void process_results(int *r)
{
    int idx = r[2] | (r[1] << 2);
    result_counts[idx]++;
}

void summarize_results()
{
    for (int rx = 0; rx <= 1; rx++) {
        for (int r0 = 0; r0 <= 1; r0++) {
            for (int r1 = 0; r1 <= 1; r1++) {
                int idx = (rx<<2) | (r1<<1) | r0;
                printf("rx=%d r0=%d r1=%d: %d\n", rx, r0, r1,
                       result_counts[idx]);
            }
        }
    }
    printf("&x = %p, &y=%p\n", &x, &y);
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1, thread2};

int thread_count = 3;
