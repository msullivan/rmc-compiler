#include <rmc++.h>

void mp_send(rmc<int> *flag, rmc<int> *data) {
    VEDGE(wdata, wflag);
    L(wdata, *data = 42);
    L(wflag, *flag = 1);
}

int mp_recv(rmc<int> *flag, int *data) {
    XEDGE(rflag, rdata);
    while (L(rflag, *flag) == 0)
        continue;
    return L(rdata, *data);
}


int cas(rmc<int> *p) {
    int i = 0;
    return p->compare_exchange_strong(i, 1);
}

int xadd_int(rmc<int> *p) {
    return p->fetch_add(10);
}

int *xadd_ptr(rmc<int *> *p) {
    return p->fetch_add(10);
}

// Make sure it works with longs also... (long overlaps with
// fetch_add on pointers so this depends on doing something to
// avoid the overlap)
long xadd_long(rmc<long> *p) {
    return p->fetch_add(10);
}
