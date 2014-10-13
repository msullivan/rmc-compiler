#include "rmc.h"

void bogus_action(int *p, int *q, int a, int b) {
    VEDGE(w1, w2);
    // This should fail since we don't allow control flow inside of
    // actions.
    L(w1, *p = a && b);
    L(w2, *q = 1);
}
