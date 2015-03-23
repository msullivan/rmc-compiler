#include <rmc.h>

extern int coin(void);

// A test that should be really bad because exponents.
// Only takes like a minute or so!
void welp(rmc_int *p, rmc_int *q) {
    VEDGE(a, b);
    L(a, rmc_store(p, 1));

    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    // 4

    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    // 8

    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    if (coin()){}
    // 16

    L(b, rmc_store(q, 1));
}
