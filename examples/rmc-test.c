#include "rmc.h"

// Some test cases that required some bogosity to not have the branches get
// optimized away.
//
// Also, if r doesn't get used usefully, that load gets optimized away.
// I can't decide whether that is totally fucked or not.

int bogus_ctrl_dep1(int *p, int *q) {
    XEDGE(read, write);

    L(read, int r = *p);
    if (r == r) {
        L(write, *q = 1);
    }

    return r;
}

// Do basically the same thing in each branch
// Looks like llvm sinks the write out of the branches but preserves
// the branches. That's fine.
int bogus_ctrl_dep2(int *p, int *q) {
    XEDGE(read, write);

    L(read, int r = *p);
    if (r) {
        L(write, *q = 1);
    } else {
        L(write, *q = 1);
    }

    return r;
}

// Have a totally ignored ctrl dep
int bogus_ctrl_dep3(int *p, int *q) {
    XEDGE(read, write);

    L(read, int r = *p);
    if (r) {};

    L(write, *q = 1);

    return r;
}

// Have a ctrl dep that is redundant
int bogus_ctrl_dep4(int *p, int *q) {
    XEDGE(read, write);

    L(read, int r = *p);
    if (r || 1) {
        L(write, *q = 1);
    }

    return r;
}

//// Some push tests
// Regular store buffering test
int sb_test1(int *p, int *q) {
    VEDGE(write, push); XEDGE(push, read);

    L(write, *p = 1);
    L(push, PUSH);
    L(read, int x = *q);

    return x;
}

// Store buffering test using pre/post
int sb_test2(int *p, int *q) {
    VEDGE(pre, push); XEDGE(post, read);

    *p = 1;
    L(push, PUSH);
    int x = *q;

    return x;
}


// Some tests of pre and post. Really I should get some RW/RMW support...
void store_release(int *ptr, int val) {
    VEDGE(pre, store);
    L(store, *ptr = val);
}
int load_acquire(int *ptr) {
    XEDGE(load, post);
    L(load, int val = *ptr);
    return val;
}

// A test where we have some overlapping things but could just do one
// cut
void overlapping(int *ptr) {
    VEDGE(a, c); VEDGE(b, d);
    L(a, *ptr = 1);
    L(b, *ptr = 2);
    L(c, *ptr = 3);
    L(d, *ptr = 4);
}

// cost 3 - make sure we can binary search down for the costs
void binarysearch_test(int *ptr) {
    VEDGE(a, b); VEDGE(b, c); VEDGE(c, d);
    L(a, *ptr = 1);
    L(b, *ptr = 2);
    L(c, *ptr = 3);
    L(d, *ptr = 4);
}

// Have a push and also a vo edge so we can make sure we take
// advantage of the push.
void push_redundant_test(int *p, int *q) {
    VEDGE(a, push); XEDGE(push, b);
    VEDGE(a, b);

    L(a, *p = 1);
    L(push, PUSH);
    L(b, *q = 2);
}

// When we insert a dependency we need to make sure that the
// thing being depended on dominates the use we insert!
// This is sort of tricky to test, though.
void ctrl_dom_test(int *p, int *q, int bs) {
    XEDGE(a, b);

    if (bs & 1) {
        L(a, int x = *p);
    }

    // Use multiple branches so that the weight will be lower if it
    // gets inserted down by the use.
    if (bs & 2) {
        if (bs & 4) {
            L(b, *q = 1);
        }
    }

    // Put in an edge that puts in an lwsync so that keeping the ctrl dep
    // close to the a load isn't useful for ensuring cross call ordering.
    VEDGE(pre, fuckoff);
    L(fuckoff, *p = 1);
}


// Looping MP recv test.
int mp_recv(int *flag, int *data) {
    int rf;
    XEDGE(rflag, rdata);
    do {
        L(rflag, rf = *flag);
    } while (rf == 0);
    L(rdata, int rd = *data);
    return rd;
}

// XXX: We fail to use an isync here because the very restricted
// pattern matching we use to detect control deps doesn't find
// this. We should be less bad.
//
// Actually, we handle this extra really badly and miscompile this
// code if we aren't depending on the branch! LLVM optimizes this into
// code that reads the flag *once* and then loops on that register.
// I'm not sure how the RMC story interacts with this sort of thing.
// Adding a XEDGE(rflag, rflag) fixes it...  As does making flag
// volatile...
int mp_recv_bang(int *flag, int *data) {
    int rf;
    XEDGE(rflag, rdata);
    do {
        L(rflag, rf = *flag);
    } while (!rf);
    L(rdata, int rd = *data);
    return rd;
}
