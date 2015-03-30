#include <rmc++.h>

void mp_send(rmc::atomic<int> *flag, rmc::atomic<int> *data) {
    VEDGE(wdata, wflag);
    L(wdata, *data = 42);
    L(wflag, *flag = 1);
}

int mp_recv(rmc::atomic<int> *flag, int *data) {
    XEDGE(rflag, rdata);
    while (L(rflag, *flag) == 0)
        continue;
    return L(rdata, *data);
}


int cas(rmc::atomic<int> *p) {
    int i = 0;
    return p->compare_exchange_strong(i, 1);
}

int xadd_int(rmc::atomic<int> *p) {
    return p->fetch_add(10);
}

int *xadd_ptr(rmc::atomic<int *> *p) {
    return p->fetch_add(10);
}

// Make sure it works with longs also... (long overlaps with
// fetch_add on pointers so this depends on doing something to
// avoid the overlap)
long xadd_long(rmc::atomic<long> *p) {
    return p->fetch_add(10);
}

// Regular store buffering test
int sb_test1(rmc::atomic<int> *p, rmc::atomic<int> *q) {
    VEDGE(write, push); XEDGE(push, read);

    L(write, *p = 1);
    L(push, rmc::push());
    int x = L(read, *q);

    return x;
}

// Store buffering test using push_here
int sb_test2(rmc::atomic<int> *p, rmc::atomic<int> *q) {
    *p = 1;
    rmc::push_here();
    int x = *q;

    return x;
}
