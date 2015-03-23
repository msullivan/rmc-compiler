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
