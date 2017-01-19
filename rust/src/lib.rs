// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

use std::sync::atomic;

// Core RMC
extern {
    fn __rmc_action_register(name: *const u8) -> i32;
    fn __rmc_action_close(reg_id: i32) -> i32;
    fn __rmc_edge_register(sort: i32, src: *const u8, dst: *const u8) -> i32;
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
pub fn _edge_register(sort: i32, src: *const u8, dst: *const u8) -> i32 {
    unsafe { __rmc_edge_register(sort, src, dst) }
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
#[macro_export]
macro_rules! PEDGE {
    ($src:ident, $dst:ident) => { RMC_EDGE!(2, $src, $dst) }
}

// RMC objects
pub const STORE_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub const LOAD_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;
pub const RMW_ORDER: atomic::Ordering = atomic::Ordering::Relaxed;


macro_rules! atomic_methods {
    ($ty:ty, $outty:ty, $rmc_name:ident, $atomic_name:path) => [
    #[inline] pub fn new(v: $ty) -> $outty { $rmc_name { v: $atomic_name(v) } }
    #[inline] pub fn load(&self) -> $ty { self.v.load(LOAD_ORDER) }
    #[inline] pub fn store(&self, val: $ty) { self.v.store(val, STORE_ORDER) }
    #[inline] pub fn swap(&self, val: $ty) -> $ty { self.v.swap(val, RMW_ORDER) }
    #[inline] pub fn compare_and_swap(&self, old: $ty, new: $ty) -> $ty {
        self.v.compare_and_swap(old, new, RMW_ORDER)
    }
    ]
}
macro_rules! atomic_methods_arithmetic {
    ($ty:ty) => [
    #[inline] pub fn fetch_add(&self, val: $ty) -> $ty { self.v.fetch_add(val, RMW_ORDER) }
    #[inline] pub fn fetch_sub(&self, val: $ty) -> $ty { self.v.fetch_sub(val, RMW_ORDER) }
    ]
}
macro_rules! atomic_methods_logical {
    ($ty:ty) => [
    #[inline] pub fn fetch_and(&self, val: $ty) -> $ty { self.v.fetch_and(val, RMW_ORDER) }
    #[inline] pub fn fetch_or(&self, val: $ty) -> $ty { self.v.fetch_or(val, RMW_ORDER) }
    #[inline] pub fn fetch_xor(&self, val: $ty) -> $ty { self.v.fetch_xor(val, RMW_ORDER) }
    ]
}

pub struct AtomicUsize { v: atomic::AtomicUsize, }
unsafe impl Sync for AtomicUsize {}
impl AtomicUsize {
    atomic_methods!(usize, AtomicUsize, AtomicUsize, atomic::AtomicUsize::new);
    atomic_methods_arithmetic!(usize);
    atomic_methods_logical!(usize);
}

pub struct AtomicIsize { v: atomic::AtomicIsize, }
unsafe impl Sync for AtomicIsize {}
impl AtomicIsize {
    atomic_methods!(isize, AtomicIsize, AtomicIsize, atomic::AtomicIsize::new);
    atomic_methods_arithmetic!(isize);
    atomic_methods_logical!(isize);
}

pub struct AtomicBool { v: atomic::AtomicBool, }
unsafe impl Sync for AtomicBool {}
impl AtomicBool {
    atomic_methods!(bool, AtomicBool, AtomicBool, atomic::AtomicBool::new);
    atomic_methods_logical!(bool);
    pub fn fetch_nand(&self, val: bool) -> bool { self.v.fetch_nand(val, RMW_ORDER) }
}

pub struct AtomicPtr<T> { v: atomic::AtomicPtr<T>, }
unsafe impl<T> Sync for AtomicPtr<T> {}
impl<T> AtomicPtr<T> {
    atomic_methods!(*mut T, AtomicPtr<T>, AtomicPtr, atomic::AtomicPtr::new);
}
