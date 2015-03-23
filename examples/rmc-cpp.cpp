#include <rmc.h>

// Ok first make sure the regular old C ones still work
void mp_send(rmc_int *flag, rmc_int *data) {
    VEDGE(wdata, wflag);
    L(wdata, rmc_store(data, 42));
    L(wflag, rmc_store(flag, 1));
}

int mp_recv(rmc_int *flag, int *data) {
    int rf;
    XEDGE(rflag, rdata);
    do {
        LS(rflag, rf = rmc_load(flag));
    } while (rf == 0);
    LS(rdata, int rd = *data);
    return rd;
}
