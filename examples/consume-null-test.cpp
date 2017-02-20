#include <rmc++.h>
#include <stdio.h>

struct foo {
    int b;
    int a;
};

// LLVM will want to propagate the knowledge that p == &foo into the
// conditional, breaking our chain. We need to make sure that doesn't
// happen. The optimization is performed by -gvn.
foo *lookup(rmc::atomic<foo *> &pp) {
    XEDGE_HERE(a, b);
    foo *p = L(a, pp);

    if (L(b, p->a) == 10) {
        return LGIVE(b, p);
    }

//    printf("ugh: %p\n", p);
//    printf("ugh\n");
    return nullptr;
}



void user(rmc::atomic<foo *> &pp) {
    foo *p = lookup(pp);
    if (p) {
        printf("thing: %d\n", p->b);
    } else {
        printf("well that's not good\n");
    }
}
