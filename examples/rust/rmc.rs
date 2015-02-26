use std::sync::atomic::{Ordering, AtomicUsize};

////////////////////
extern {
    fn __rmc_action_register(name: *const u8) -> i32;
    fn __rmc_action_close(reg_id: i32) -> i32;
    fn __rmc_edge_register(is_vis: i32, src: *const u8, dst: *const u8) -> i32;
    fn __rmc_push() -> i32;
}

macro_rules! ident_cstr {
    ($i:ident) => { concat!(stringify!($i), "\0").as_ptr() }
}

macro_rules! L {
    ($label:ident, $e:expr) => {{
        let id = __rmc_action_register(ident_cstr!($label));
        let v = $e;
        __rmc_action_close(id);
        v
    }}
}

macro_rules! PUSH { () => { __rmc_push() } }

macro_rules! RMC_EDGE {
    ($sort:expr, $src:ident, $dst:ident) => {
        __rmc_edge_register($sort, ident_cstr!($src), ident_cstr!($dst))
    }
}
macro_rules! XEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(0, $src, $dst) }
}
macro_rules! VEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(1, $src, $dst) }
}

/////////////////////////////////


pub unsafe fn mp_send_test(data: *mut AtomicUsize, flag: *mut AtomicUsize) {
    VEDGE!(wdata, wflag);
    L!(wdata, (*data).store(42, Ordering::Relaxed));
    L!(wflag, (*flag).store(1, Ordering::Relaxed));
}
