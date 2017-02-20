// Try a leapfrogging thing
// We are looking for ry1==1, ry2==2, rx2==0

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
    int ry = y;
    y = 2;
    return ry;
}

int thread2()
{
    int ry;
    // Can't do a ctrl dep because it might not happen
    //while ((ry = y) != 2);
    ry = y;
    int rx = x;//*bogus_dep(&x, ry);
    return (rx<<2) | ry;
}


void reset()
{
    x = y = 0;
}

// Formatting results
int result_counts[16];

void process_results(int *r)
{
    int idx = r[2] | (r[1] << 3);
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
test_fn *test_fns[] = {thread0, thread1, thread2};

int thread_count = 3;
