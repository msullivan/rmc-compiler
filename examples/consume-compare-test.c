#include <stdlib.h>
#include <rmc.h>

int foo;

typedef _Rmc(int*) rmc_pint;


// LLVM will want to propagate the knowledge that p == &foo into the
// conditional, breaking our chain. We need to make sure that doesn't
// happen. The optimization is performed by -gvn.
int test(rmc_pint* pp) {
    XEDGE_HERE(a, b);
    int *p = L(a, rmc_load(pp));
    if (p == &foo) {
        return L(b, *p);
    }
    return -1;
}

// UGHHHHHH clang will back propagate equality information
int test2(rmc_pint* pp) {
    XEDGE_HERE(a, b);
    int *p = L(a, rmc_load(pp));
    int a = L(b, *p);
    if (p != &foo) abort();
    return a;
}



// UGHHHHHH clang will back propagate equality information
// through fucking function inlining
int *test3_part(rmc_pint* pp, int *outp) {
    XEDGE_HERE(a, b);
    int *p = L(a, rmc_load(pp));
    int a = L(b, *p);
    *outp = a;
    return p;
}

int test3(rmc_pint* pp) {
    int a;
    int *p = test3_part(pp, &a);
    if (p != &foo) abort();
    return a;
}

int test4_part(rmc_pint* pp, int **outpp) {
    XEDGE_HERE(a, b);
    int *p = L(a, rmc_load(pp));
    int a = L(b, *p);
    *outpp = p;
    return a;
}

int test4(rmc_pint* pp) {
    int *p;
    int a = test4_part(pp, &p);
    if (p != &foo) abort();
    return a;
}
