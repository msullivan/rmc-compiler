#[macro_use]
extern crate rmc;

use rmc::RmcUsize;

pub fn mp_send_test(data: &RmcUsize, flag: &RmcUsize) {
    VEDGE!(wdata, wflag);
    L!(wdata, data.store(42));
    L!(wflag, flag.store(1));
}
