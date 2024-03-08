use core::fmt;
use std::{
    cell::UnsafeCell,
    ffi::{c_char, c_double, CStr, CString},
    hash::{Hash, Hasher},
    ptr::null_mut,
    rc::Rc,
};

use crate::util::*;

pub type KiwiVar = UnsafeCell<KiwiVarData>;

#[repr(C)]
#[derive(Clone, Debug, PartialEq)]
pub struct KiwiVarData {
    pub value_: f64,
    pub name_: *mut c_char,
}

impl Drop for KiwiVarData {
    fn drop(&mut self) {
        if !self.name_.is_null() {
            unsafe {
                drop(CString::from_raw(self.name_));
            }
        }
    }
}

#[derive(Clone)]
pub(crate) struct SolverVariable(Rc<KiwiVar>);

impl SolverVariable {
    #[inline]
    pub(crate) unsafe fn from_raw(ptr: *const KiwiVar) -> Self {
        Rc::increment_strong_count(ptr);
        Self(Rc::from_raw(ptr))
    }

    #[inline]
    pub(crate) unsafe fn try_from_raw(ptr: *const KiwiVar) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            Some(Self::from_raw(ptr))
        }
    }

    #[inline]
    pub(crate) unsafe fn set_value(&self, value: &f64) {
        (*self.0.get()).value_ = *value;
    }
}

pub(crate) unsafe fn var_retain_raw(var: *const KiwiVar) -> *const KiwiVar {
    let var = not_null(var).unwrap();
    Rc::increment_strong_count(var);
    var
}

impl Eq for SolverVariable {}

impl PartialEq for SolverVariable {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.0, &other.0)
    }
}

impl Hash for SolverVariable {
    fn hash<H: Hasher>(&self, state: &mut H) {
        Rc::as_ptr(&self.0).hash(state);
    }
}

impl fmt::Debug for SolverVariable {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("SolverVariable")
            .field(unsafe { &CStr::from_ptr((*self.0.get()).name_) })
            .finish()
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_new(name: *const c_char) -> *const KiwiVar {
    Rc::into_raw(Rc::new(UnsafeCell::new(KiwiVarData {
        value_: 0.0,
        name_: match not_null(name) {
            None => null_mut(),
            Some(name) => CStr::from_ptr(name).to_owned().into_raw(),
        },
    })))
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_release(var: *const KiwiVar) {
    if let Some(var) = not_null(var) {
        Rc::decrement_strong_count(var);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_retain(var: *const KiwiVar) {
    if let Some(var) = not_null(var) {
        Rc::increment_strong_count(var);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_value(var: *const KiwiVar) -> c_double {
    match not_null_ref(var) {
        Some(var) => (*var.get()).value_,
        None => c_double::NAN,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_set_value(var: *const KiwiVar, value: c_double) {
    if let Some(var) = not_null_ref(var) {
        (*var.get()).value_ = value;
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_name(var: *const KiwiVar) -> *const c_char {
    match not_null_ref(var) {
        None => CStr::from_bytes_with_nul_unchecked(b"\0").as_ptr(),
        Some(var) => (*var.get()).name_,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_set_name(var: *const KiwiVar, name: *const c_char) {
    if let Some(var) = not_null_ref(var) {
        (*var.get()).name_ = match not_null(name) {
            None => null_mut(),
            Some(name) => CStr::from_ptr(name).to_owned().into_raw(),
        }
    }
}
