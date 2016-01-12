// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#![feature(plugin_registrar, rustc_private)]
extern crate rustc;
use rustc::plugin::Registry;
extern crate rustc_llvm;
use std::ffi::{CString,CStr};

use std::process::Command;

#[plugin_registrar]
pub fn plugin_registrar(reg: &mut Registry) {
    let output = Command::new("rmc-config").arg("--lib").output().
        unwrap_or_else(|e| {
            panic!("failed to run rmc-config: {}", e)
        });

    let s = CString::new(output.stdout).unwrap();
    unsafe {
        if rustc_llvm::LLVMRustLoadDynamicLibrary(s.as_ptr()) == 0 {
            let err = rustc_llvm::LLVMRustGetLastError();
            let s = CStr::from_ptr(err).to_string_lossy().into_owned();
            panic!("couldn't load rmc library: {}", s)
        }
    }
    reg.register_llvm_pass("realize-rmc");
    reg.register_llvm_pass("simplifycfg");
}
