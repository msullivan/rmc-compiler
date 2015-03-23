#include <rmc.h>

int load_acquire(rmc_int *ptr) {
    XEDGE(load, post);
    int val = L(load, rmc_load(ptr));
    return val;
}

int test(rmc_int *a, rmc_int *b) {
    int r1 = load_acquire(a);
    int r2 = load_acquire(b);
    return r1+r2;
}
