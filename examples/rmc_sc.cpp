#include <rmc++.h>

void mp_send(rmc::sc_atomic<int> *flag, int *data) {
    VEDGE(wdata, wflag);
    L(wdata, *data = 42);
    L(wflag, *flag = 1);
}


int sb_test1(rmc::sc_atomic<int> *p, rmc::sc_atomic<int> *q) {
    *p = 1;
    return *q;
}
