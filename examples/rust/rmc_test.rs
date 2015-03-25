use std::sync::atomic::{Ordering, AtomicUsize};

#[macro_use]
mod rmc;


pub unsafe fn mp_send_test(data: *mut AtomicUsize, flag: *mut AtomicUsize) {
    VEDGE!(wdata, wflag);
    L!(wdata, (*data).store(42, Ordering::Relaxed));
    L!(wflag, (*flag).store(1, Ordering::Relaxed));
}
