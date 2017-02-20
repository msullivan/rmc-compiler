// Try a leapfrogging thing
// We are looking for ry1==1, ry2==2, rx2==0
// Try leapfrogging on only two threads

#include "atomic.h"
#include <stdio.h>


volatile long x PADDED = 1;
volatile long y PADDED = 1;

int thread0()
{
    x = 1;
    smp_mb();
    y = 1;
    return 0;
}

int thread1()
{
    int ry1 = y;
    y = 2;
    int ry2 = y;
    int rx = *bogus_dep(&x, ry2);
    return (ry1<<3) | (rx<<2) | ry2;
}

void reset()
{
    x = y = 0;
}

// Formatting results
int result_counts[16];

void process_results(int *r)
{
    int idx = r[1];
    result_counts[idx]++;
}

void summarize_results()
{
    for (int ry1 = 0; ry1 <= 1; ry1++) {
        for (int ry2 = 0; ry2 <= 2; ry2++) {
            for (int rx2 = 0; rx2 <= 1; rx2++) {
                int idx = (ry1<<3) | (rx2<<2) | ry2;
                printf("ry1=%d ry2=%d rx2=%d: %d\n", ry1, ry2, rx2,
                       result_counts[idx]);
            }
        }
    }
    printf("&x = %p, &y=%p\n", &x, &y);
}


typedef int (test_fn)();
test_fn *test_fns[] = {thread0, thread1};

int thread_count = 2;
