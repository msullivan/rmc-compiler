#include <rmc.h>

int foo;

// LLVM will want to propagate the knowledge that p == &foo into the
// conditional, breaking our chain. We need to make sure that doesn't
// happen. The optimization is performed by -gvn.
int test(_Rmc(int *)* pp) {
    XEDGE(a, b);
    int *p = L(a, rmc_load(pp));
    if (p == &foo) {
        return L(b, *p);
    }
    return -1;
}
