//! Path: clouds-labs/labs/tailslayer/rust/src/allocator.rs
//! Purpose: Dual-channel memory allocator with 2MB hugepage support and mlock.

use libc::{c_void, mmap, mlock, munmap, MAP_ANONYMOUS, MAP_FAILED, MAP_HUGETLB, MAP_PRIVATE, PROT_READ, PROT_WRITE};
use std::ptr::NonNull;

pub const HUGEPAGE_2M: usize = 1 << 21;
pub const CHANNEL_OFFSET: usize = 256;

pub struct HedgedBuffer {
    ptr: NonNull<u8>,
    len: usize,
    hugepage: bool,
}

unsafe impl Send for HedgedBuffer {}
unsafe impl Sync for HedgedBuffer {}

impl HedgedBuffer {
    pub fn allocate(len: usize) -> Result<Self, String> {
        let alloc_len = ((len + HUGEPAGE_2M - 1) / HUGEPAGE_2M) * HUGEPAGE_2M;
        unsafe {
            let mut ptr = mmap(
                std::ptr::null_mut(),
                alloc_len,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                -1,
                0,
            );
            let mut huge = true;
            if ptr == MAP_FAILED {
                huge = false;
                ptr = mmap(
                    std::ptr::null_mut(),
                    alloc_len,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    -1,
                    0,
                );
            }
            if ptr == MAP_FAILED {
                return Err(format!("mmap failed: {}", std::io::Error::last_os_error()));
            }

            mlock(ptr, alloc_len);

            let byte_slice = std::slice::from_raw_parts_mut(ptr as *mut u8, alloc_len);
            for chunk in byte_slice.chunks_mut(4096) {
                chunk[0] = 0x5A;
            }

            Ok(Self {
                ptr: NonNull::new_unchecked(ptr as *mut u8),
                len: alloc_len,
                hugepage: huge,
            })
        }
    }

    #[inline]
    pub fn as_ptr(&self) -> *const u8 {
        self.ptr.as_ptr()
    }

    #[inline]
    pub fn as_mut_ptr(&self) -> *mut u8 {
        self.ptr.as_ptr()
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.len
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    #[inline]
    pub fn is_hugepage(&self) -> bool {
        self.hugepage
    }

    #[inline(always)]
    pub fn channel_a(&self) -> *const u8 {
        self.ptr.as_ptr()
    }

    #[inline(always)]
    pub fn channel_b(&self) -> *const u8 {
        unsafe { self.ptr.as_ptr().add(CHANNEL_OFFSET) }
    }
}

impl Drop for HedgedBuffer {
    fn drop(&mut self) {
        unsafe {
            munmap(self.ptr.as_ptr() as *mut c_void, self.len);
        }
    }
}
