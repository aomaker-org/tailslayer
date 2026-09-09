//! Path: clouds-labs/labs/tailslayer/rust/src/arch.rs
//! Purpose: Hardware cycle counter and cache management primitives.

#[inline(always)]
pub fn read_cycles() -> u64 {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        std::arch::x86_64::_mm_lfence();
        std::arch::x86_64::_rdtsc()
    }
    #[cfg(target_arch = "aarch64")]
    unsafe {
        let val: u64;
        std::arch::asm!("isb; mrs {}, cntvct_el0", out(reg) val, options(nostack, nomem));
        val
    }
    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
    {
        use std::time::{SystemTime, UNIX_EPOCH};
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos() as u64
    }
}

#[inline(always)]
pub unsafe fn clflush(ptr: *const u8) {
    #[cfg(target_arch = "x86_64")]
    {
        std::arch::x86_64::_mm_clflush(ptr as *const _);
        std::arch::x86_64::_mm_mfence();
    }
    #[cfg(target_arch = "aarch64")]
    {
        std::arch::asm!("dc civac, {}; dsb ish", in(reg) ptr, options(nostack));
    }
    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64")))]
    {
        let _ = ptr;
    }
}

pub fn calibrate_timer_ghz() -> f64 {
    let t0 = read_cycles();
    let sys_t0 = std::time::Instant::now();
    std::thread::sleep(std::time::Duration::from_millis(50));
    let t1 = read_cycles();
    let elapsed_sec = sys_t0.elapsed().as_secs_f64();
    let cycles = (t1.saturating_sub(t0)) as f64;
    (cycles / elapsed_sec) / 1e9
}
