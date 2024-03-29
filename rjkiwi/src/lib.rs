#[cfg(feature = "mimalloc")]
#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;

pub mod expr;
pub mod solver;
mod util;
pub mod var;

pub mod mem {
    use std::{
        ffi::{c_char, CString},
        ptr::NonNull,
    };

    #[no_mangle]
    pub unsafe extern "C" fn kiwi_str_release(p: *mut c_char) {
        if let Some(p) = NonNull::new(p) {
            drop(CString::from_raw(p.as_ptr()));
        }
    }
}

pub mod ffi {
    pub use crate::expr::*;
    pub use crate::mem::*;
    pub use crate::solver::*;
    pub use crate::var::*;
}
