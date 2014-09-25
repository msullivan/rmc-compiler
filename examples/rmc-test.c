#include "rmc.h"

// Some test cases that required some bogosity to not have the branches get
// optimized away.
//
// Also, if r doesn't get used usefully, that load gets optimized away.
// I can't decide whether that is totally fucked or not.

int global_p, global_q;

int bogus_ctrl_dep1() {
    XEDGE(read, write);

    L(read, int r = global_p);
    if (r == r) {
        L(write, global_q = 1);
    }

    return r;
}

// Do basically the same thing in each branch
int bogus_ctrl_dep2() {
    XEDGE(read, write);

    L(read, int r = global_p);
    if (r) {
        L(write, global_q = 1);
    } else {
        L(write, global_q = 1);
    }

    return r;
}

// Have a bogus ctrl dep
int bogus_ctrl_dep3() {
    XEDGE(read, write);

    L(read, int r = global_p);
    if (r) {};

    L(write, global_q = 1);

    return r;
}
