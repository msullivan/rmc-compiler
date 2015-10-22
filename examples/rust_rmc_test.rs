#[macro_use]
extern crate rmc;

// For testing rust's RMC stuff without going through cargo, try
// something like:
// LD_PRELOAD=../RMC.so rustc -L ../rust/target/release/ -O --emit llvm-ir --crate-type lib -C 'passes=realize-rmc simplifycfg' rust_rmc_test.rs

pub fn mp_send_test(data: &rmc::AtomicUsize, flag: &rmc::AtomicUsize) {
    VEDGE!(wdata, wflag);
    L!(wdata, data.store(42));
    L!(wflag, flag.store(1));
}
