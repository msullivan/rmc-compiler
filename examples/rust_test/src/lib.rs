#![feature(plugin)]
#![plugin(rmc_plugin)]
#[macro_use]
extern crate rmc;

pub fn mp_send_test(data: &rmc::AtomicUsize, flag: &rmc::AtomicUsize) {
    VEDGE!(wdata, wflag);
    L!(wdata, data.store(42));
    L!(wflag, flag.store(1));
}
