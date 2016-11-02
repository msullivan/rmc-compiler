#include <rmc.h>

void crit_edge(int b1, int b2) {
    extern int some_func(int n);
    // Test that we can properly break a critical edge and insert a
    // sync that runs on just the right path.
    // In this program, there are CFG edges a->b, a->d, and c->d
    // and an RMC edge a->d. We want to insert a sync just between
    // a->d, but this is complicated by the existence of a->b and c->d
    // edges in the CFG: if we put the sync at the end of a, it runs
    // even when we go to b and if we put it at the start of d it runs
    // when we came from c. (That is, a->d is a "critical edge", since
    // its source node has multiple successors and its destination
    // node has multiple predecessors.)
    // The solution to this is to "break" the critical edge by adding
    // an empty basic block e in between a and d. We can then add the
    // sync into e.

    // It turns out that this is trivially easy to accomplish: LLVM
    // has a pass called "BreakCriticalEdges", and we make it a
    // dependency.

    XEDGE_HERE(a, d);
    if (b1) {
        L(a, some_func(0)); // a
        if (b2) {
            some_func(1); // b
            return;
        }
    } else {
        some_func(2); // c
    }
    L(d, some_func(3)); // d
}
