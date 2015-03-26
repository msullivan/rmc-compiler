use std::sync::atomic;
use std::sync::atomic::{AtomicUsize,AtomicIsize,AtomicBool,AtomicPtr};

// Core RMC
extern {
    fn __rmc_action_register(name: *const u8) -> i32;
    fn __rmc_action_close(reg_id: i32) -> i32;
    fn __rmc_edge_register(is_vis: i32, src: *const u8, dst: *const u8) -> i32;
    fn __rmc_push() -> i32;
}

#[inline(always)]
pub fn push() { unsafe { __rmc_push(); } }

#[inline(always)]
pub fn _action_register(name: *const u8) -> i32 {
    unsafe { __rmc_action_register(name) }
}
#[inline(always)]
pub fn _action_close(reg_id: i32) -> i32 {
    unsafe { __rmc_action_close(reg_id) }
}
#[inline(always)]
pub fn _edge_register(is_vis: i32, src: *const u8, dst: *const u8) -> i32 {
    unsafe { __rmc_edge_register(is_vis, src, dst) }
}

#[macro_export]
macro_rules! ident_cstr {
    ($i:ident) => { concat!(stringify!($i), "\0").as_ptr() }
}

#[macro_export]
macro_rules! L {
    ($label:ident, $e:expr) => {{
        let id = $crate::_action_register(ident_cstr!($label));
        let v = $e;
        $crate::_action_close(id);
        v
    }}
}

#[macro_export]
macro_rules! RMC_EDGE {
    ($sort:expr, $src:ident, $dst:ident) => {
        $crate::_edge_register($sort, ident_cstr!($src), ident_cstr!($dst))
    }
}
#[macro_export]
macro_rules! XEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(0, $src, $dst) }
}
#[macro_export]
macro_rules! VEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(1, $src, $dst) }
}

// RMC objects
pub static STORE_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub static LOAD_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub static RMW_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;


macro_rules! atomic_methods {
    ($ty:ty, $outty:ty, $rmc_name:ident, $atomic_name:ident) => [
    pub fn new(v: $ty) -> $outty { $rmc_name { v: $atomic_name::new(v) } }
    pub fn load(&self) -> $ty { self.v.load(LOAD_ORDER) }
    pub fn store(&self, val: $ty) { self.v.store(val, STORE_ORDER) }
    pub fn swap(&self, val: $ty) -> $ty { self.v.swap(val, RMW_ORDER) }
    pub fn compare_and_swap(&self, old: $ty, new: $ty) -> $ty {
        self.v.compare_and_swap(old, new, RMW_ORDER)
    }
    ]
}
macro_rules! atomic_methods_arithmetic {
    ($ty:ty) => [
    pub fn fetch_add(&self, val: $ty) -> $ty { self.v.fetch_add(val, RMW_ORDER) }
    pub fn fetch_sub(&self, val: $ty) -> $ty { self.v.fetch_sub(val, RMW_ORDER) }
    ]
}
macro_rules! atomic_methods_logical {
    ($ty:ty) => [
    pub fn fetch_and(&self, val: $ty) -> $ty { self.v.fetch_and(val, RMW_ORDER) }
    pub fn fetch_or(&self, val: $ty) -> $ty { self.v.fetch_or(val, RMW_ORDER) }
    pub fn fetch_xor(&self, val: $ty) -> $ty { self.v.fetch_xor(val, RMW_ORDER) }
    ]
}

pub struct RmcUsize { v: AtomicUsize, }
unsafe impl Sync for RmcUsize {}
impl RmcUsize {
    atomic_methods!(usize, RmcUsize, RmcUsize, AtomicUsize);
    atomic_methods_arithmetic!(usize);
    atomic_methods_logical!(usize);
}

pub struct RmcIsize { v: AtomicIsize, }
unsafe impl Sync for RmcIsize {}
impl RmcIsize {
    atomic_methods!(isize, RmcIsize, RmcIsize, AtomicIsize);
    atomic_methods_arithmetic!(isize);
    atomic_methods_logical!(isize);
}

pub struct RmcBool { v: AtomicBool, }
unsafe impl Sync for RmcBool {}
impl RmcBool {
    atomic_methods!(bool, RmcBool, RmcBool, AtomicBool);
    atomic_methods_logical!(bool);
    pub fn fetch_nand(&self, val: bool) -> bool { self.v.fetch_nand(val, RMW_ORDER) }
}

pub struct RmcPtr<T> { v: atomic::AtomicPtr<T>, }
unsafe impl<T> Sync for RmcPtr<T> {}
impl<T> RmcPtr<T> {
    atomic_methods!(*mut T, RmcPtr<T>, RmcPtr, AtomicPtr);
}
