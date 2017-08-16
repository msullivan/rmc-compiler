#include <rmc++.h>

int version1(rmc::atomic<bool> &flag, rmc::atomic<int> &data) {
    XEDGE(rflag, rdata);
    while (!L(rflag, flag)) continue;
    return L(rdata, data);
}

int version2(rmc::atomic<bool> &flag, rmc::atomic<int> &data) {
    XEDGE(rflag, rdata);
    XEDGE(rflag, rflag);
    while (!L(rflag, flag)) continue;
    return L(rdata, data);
}

int version3(rmc::atomic<bool> &flag, rmc::atomic<int> &data) {
    XEDGE(rflag, post);
    while (!L(rflag, flag)) continue;
    return L(rdata, data);
}
