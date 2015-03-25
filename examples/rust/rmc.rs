use std::sync::atomic;

extern {
    fn __rmc_action_register(name: *const u8) -> i32;
    fn __rmc_action_close(reg_id: i32) -> i32;
    fn __rmc_edge_register(is_vis: i32, src: *const u8, dst: *const u8) -> i32;
    fn __rmc_push() -> i32;
}

pub fn push() { unsafe { __rmc_push(); } }

pub fn _action_register(name: *const u8) -> i32 {
    unsafe { __rmc_action_register(name) }
}
pub fn _action_close(reg_id: i32) -> i32 {
    unsafe { __rmc_action_close(reg_id) }
}
pub fn _edge_register(is_vis: i32, src: *const u8, dst: *const u8) -> i32 {
    unsafe { __rmc_edge_register(is_vis, src, dst) }
}

macro_rules! ident_cstr {
    ($i:ident) => { concat!(stringify!($i), "\0").as_ptr() }
}

macro_rules! L {
    ($label:ident, $e:expr) => {{
        let id = $crate::rmc::_action_register(ident_cstr!($label));
        let v = $e;
        $crate::rmc::_action_close(id);
        v
    }}
}


macro_rules! RMC_EDGE {
    ($sort:expr, $src:ident, $dst:ident) => {
        $crate::rmc::_edge_register($sort, ident_cstr!($src), ident_cstr!($dst))
    }
}
macro_rules! XEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(0, $src, $dst) }
}
macro_rules! VEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(1, $src, $dst) }
}

pub static STORE_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub static LOAD_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub static RMW_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
