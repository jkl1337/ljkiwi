#[cfg(feature = "mimalloc")]
#[global_allocator]
static GLOBAL: mimalloc::MiMalloc = mimalloc::MiMalloc;

pub mod expr;
pub mod solver;
mod util;
pub mod var;

mod mem {
    use std::ffi::{c_char, CString};

    use crate::util::not_null_mut;

    #[no_mangle]
    pub unsafe extern "C" fn kiwi_str_release(p: *mut c_char) {
        if let Some(p) = not_null_mut(p) {
            drop(CString::from_raw(p));
        }
    }
}
