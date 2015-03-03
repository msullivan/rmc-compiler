#include "rmc.h"

int load_acquire(int *ptr) {
    XEDGE(load, post);
    int val = L(load, *ptr);
    return val;
}

int test(int *a, int *b) {
    int r1 = load_acquire(a);
    int r2 = load_acquire(b);
    return r1+r2;
}
