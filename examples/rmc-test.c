#include <rmc.h>

// Some test cases that required some bogosity to not have the branches get
// optimized away.

int bogus_ctrl_dep1(rmc_int *p, rmc_int *q) {
    XEDGE(read, write);

    int r = L(read, rmc_load(p));
    if (r == r) {
        L(write, rmc_store(q, 1));
    }

    return r;
}

// Do basically the same thing in each branch
// Looks like llvm sinks the write out of the branches but preserves
// the branches. That's fine.
int bogus_ctrl_dep2(rmc_int *p, rmc_int *q) {
    XEDGE(read, write);

    int r = L(read, rmc_load(p));
    if (r) {
        L(write, rmc_store(q, 1));
    } else {
        L(write, rmc_store(q, 1));
    }

    return r;
}

// Have a totally ignored ctrl dep
int bogus_ctrl_dep3(rmc_int *p, rmc_int *q) {
    XEDGE(read, write);

    int r = L(read, rmc_load(p));
    if (r) {}

    L(write, rmc_store(q, 1));

    return r;
}

// Have a ctrl dep that is redundant
int bogus_ctrl_dep4(rmc_int *p, rmc_int *q) {
    XEDGE(read, write);

    int r = L(read, rmc_load(p));
    if (r || 1) {
        L(write, rmc_store(q, 1));
    }

    return r;
}

// A tautological comparison that we can't disguise just by disguising
// the input.
int bogus_ctrl_dep5(rmc_uint *p, rmc_uint *q) {
    XEDGE(read, write);

    unsigned r = L(read, rmc_load(p));
    if (r >= 0) {
        L(write, rmc_store(q, 1));
    }

    return r;
}

// Another one
int bogus_ctrl_dep6(_Rmc(unsigned short) *p, rmc_uint *q) {
    XEDGE(read, write);

    unsigned short r = L(read, rmc_load(p));
    if (r != 10000000) {
        L(write, rmc_store(q, 1));
    }

    return r;
}


//// Some push tests
// Regular store buffering test
int sb_test1(rmc_int *p, rmc_int *q) {
    VEDGE(write, push); XEDGE(push, read);

    L(write, rmc_store(p, 1));
    L(push, rmc_push());
    int x = L(read, rmc_load(q));

    return x;
}

// Store buffering test using pre/post
int sb_test2(rmc_int *p, rmc_int *q) {
    VEDGE(pre, push); XEDGE(push, post);

    rmc_store(p, 1);
    L(push, rmc_push());
    int x = rmc_load(q);

    return x;
}

// Store buffering test using PEDGE
int sb_test3(rmc_int *p, rmc_int *q) {
    PEDGE(write, read);

    L(write, rmc_store(p, 1));
    int x = L(read, rmc_load(q));

    return x;
}

// A test with PEDGE that makes sure that transitive stuff works right
// into a PEDGE.
int sb_test4(rmc_int *p, rmc_int *q) {
    VEDGE(write, bs);
    PEDGE(bs, read);

    L(write, rmc_store(p, 1));
    L(bs, 0);
    int x = L(read, rmc_load(q));

    return x;
}

int sb_test5(rmc_int *p, rmc_int *q) {
    rmc_store_sc(p, 1);
    int x = rmc_load_sc(q);

    return x;
}

// Some tests of pre and post.
void store_release(rmc_int *ptr, int val) {
    VEDGE(pre, store);
    L(store, rmc_store(ptr, val));
}
int load_acquire(rmc_int *ptr) {
    XEDGE(load, post);
    int val = L(load, rmc_load(ptr));
    return val;
}

// A test where we have some overlapping things but could just do one
// cut. The greedy no-smt version inserts two.
void overlapping(rmc_int *ptr) {
    // Because everything is bad, this is sensitive to the order these
    // appear in.
    VEDGE(a, d);
    VEDGE(a, c);
    L(a, rmc_store(ptr, 1));
    L(b, rmc_store(ptr, 2));
    L(c, rmc_store(ptr, 3));
    L(d, rmc_store(ptr, 4));
}

// cost 3 - make sure we can binary search down for the costs
void binarysearch_test(rmc_int *ptr) {
    VEDGE(a, b); VEDGE(b, c); VEDGE(c, d);
    L(a, rmc_store(ptr, 1));
    L(b, rmc_store(ptr, 2));
    L(c, rmc_store(ptr, 3));
    L(d, rmc_store(ptr, 4));
}

// Have a push and also a vo edge so we can make sure we take
// advantage of the push.
void push_redundant_test(rmc_int *p, rmc_int *q) {
    VEDGE(a, push); XEDGE(push, b);
    VEDGE(a, b);

    L(a, rmc_store(p, 1));
    L(push, rmc_push());
    L(b, rmc_store(q, 2));
}

// When we insert a dependency we need to make sure that the
// thing being depended on dominates the use we insert!
// This is sort of tricky to test, though.
void ctrl_dom_test(rmc_int *p, rmc_int *q, int bs) {
    XEDGE(a, b);

    if (bs & 1) {
        int x = L(a, rmc_load(p));
        (void)x;
    }

    // Use multiple branches so that the weight will be lower if it
    // gets inserted down by the use.
    if (bs & 2) {
        if (bs & 4) {
            L(b, rmc_store(q, 1));
        }
    }

    // Put in an edge that puts in an lwsync so that keeping the ctrl dep
    // close to the a load isn't useful for ensuring cross call ordering.
    VEDGE(pre, force);
    L(force, rmc_store(p, 1));
}

// MP sending
void mp_send(rmc_int *flag, rmc_int *data) {
    VEDGE(wdata, wflag);
    L(wdata, rmc_store(data, 42));
    L(wflag, rmc_store(flag, 1));
}

// Looping MP recv test.
int mp_recv(rmc_int *flag, int *data) {
    int rf;
    XEDGE(rflag, rdata);
    do {
        LS(rflag, rf = rmc_load(flag));
    } while (rf == 0);
    LS(rdata, int rd = *data);
    return rd;
}

// XXX: We fail to use an isync here because the very restricted
// pattern matching we use to detect control deps doesn't find
// this. We should be less bad.
int mp_recv_bang(rmc_int *flag, int *data) {
    int rf;
    XEDGE(rflag, rdata);
    do {
        L(rflag, rf = rmc_load(flag));
    } while (!rf);
    int rd = L(rdata, *data);
    return rd;
}

// Hm. This is nicer.
int mp_recv_le(rmc_int *flag, int *data) {
    XEDGE(rflag, rdata);
    while (L(rflag, rmc_load(flag)) == 0)
        continue;
    return L(rdata, *data);
}


// Consume style stuff
int recv_consume(_Rmc(int *)*pdata) {
    XEDGE_HERE(rp, rdata);
    int *p = L(rp, rmc_load(pdata));
    int rd = L(rdata, *p);
    return rd;
}

// Same as above but indexing
int recv_consume2(int *parray, rmc_int *pidx) {
    XEDGE_HERE(rp, rdata);
    int idx = L(rp, rmc_load(pidx));
    int rd = L(rdata, parray[idx]);
    return rd;
}

int recv_consume_loop(_Rmc(int *)*pdata) {
    // This doesn't work, since *every* rp needs to precede rdata and
    // only one of them has a dep...
    XEDGE_HERE(rp, rdata);
    int *p;
    while ((p = L(rp, rmc_load(pdata))) == 0)
        ;
    int rd = L(rdata, *p);
    return rd;
}

int recv_consume_loop_inner(_Rmc(int *)*pdata) {
    for (;;) {
        // But if we bind the edge *inside* of the loop, it works!
        XEDGE_HERE(rp, rdata);
        int *p = L(rp, rmc_load(pdata));
        if (p) return L(rdata, *p);
    }
}

int recv_consume_loop_weird(_Rmc(int *)*pdata) {
    // This does work but is maybe a little weird. Even though the
    // XEDGE_HERE dominates L(rdata), L(rdata) is certainly not in its
    // scope in a C sense...
    int *p;
    for (;;) {
        XEDGE_HERE(rp, rdata);
        if ((p = L(rp, rmc_load(pdata))) != 0) {
            break;
        }
    }
    int rd = L(rdata, *p);
    return rd;
}


int recv_consume_twice(int *parray, rmc_int *pidx) {
    // We don't handle this well yet because of how we handle transitivity
    // in the SMT version.
    XEDGE_HERE(rp, rdata);
    XEDGE_HERE(rdata, rdata2);
    int idx = L(rp, rmc_load(pidx));
    int rd = L(rdata, parray[idx]);
    rd = L(rdata2, parray[rd]);
    return rd;
}

int recv_consume_tricky_binding(_Rmc(int *)*pdata) {
    // Even though there is an edge bound inside that won't require an
    // lwsync, the stupid transitive edges through the no-ops do.
    XEDGE(rp, dummy);
    XEDGE(dummy, rp);
    XEDGE_HERE(rp, rdata);
    int *p = L(rp, rmc_load(pdata));
    L(dummy, 0);
    int rd = L(rdata, *p);
    return rd;
}
