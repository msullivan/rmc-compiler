#include <rmc.h>

int nus;

extern int lol();

void multiblock_1(int b, rmc_int *p, rmc_int *q) {
    VEDGE(ainner, post);
    VEDGE(a, b);

    LS(a, {
        L(ainner, lol());
        if (b) { rmc_store(q, 1); } else { rmc_store(p, 1); }
    });
    L(b, rmc_store(q, 2));
}
