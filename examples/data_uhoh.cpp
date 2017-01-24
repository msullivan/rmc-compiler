#include <rmc++.h>

void lol(int n);

void nus(rmc::atomic<int*> &a, int *p) {
    // A -> B -*-> B, needs dmb
    XEDGE_HERE(a, b);
    int *q = L(a, a);
    while (1) {
        int n = L(b, *q);
        q = p;
        lol(n);
    }
}

// this one *shouldn't* require a dmb!!
void nus2(rmc::atomic<int*> &a) {
    // A -> B -*-> B, shouldn't need dmb
    XEDGE_HERE(a, b);
    int *q = L(a, a);
    while (1) {
        int n = L(b, *q);
        lol(n);
    }
}

void nus3(rmc::atomic<int*> &a, int *p, int i) {
    // A -> C -*-> C -> B, needs dmb
    XEDGE_HERE(a, b);
    int *q = L(a, a);

    int *r;
    // r = q if we go through this loop exactly once
    // if the loop condition repeats then r = p
    do {
        r = q;
        q = p;
    } while (--i);

    int n = L(b, *r);
    lol(n);
}


void nus4(rmc::atomic<int*> &a, int *p, int i) {
    // We should emit just one dmb, along the branch path.
    XEDGE_HERE(a, b);
    int *q = L(a, a);

    if (i) {
        lol(10);
        q = p;
    }

    int n = L(b, *q);
    lol(n);
}

void nus5(rmc::atomic<int*> &a, int *p) {
    // Should only need the one dmb.
    XEDGE(a, b);
    int *q = L(a, a);
    int n = L(b, *q);
    VEDGE(c, d); LS(c, lol(n)); LS(d, lol(n));
}
