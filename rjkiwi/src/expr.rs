use std::{ffi::{c_double, c_int, c_void}, rc::Rc};

use crate::{util::*, var::KiwiVar};

pub type KiwiExpressionPtr = KiwiExpression<[KiwiTerm; 0]>;

#[repr(C)]
#[derive(Debug)]
pub struct KiwiTerm {
    pub var: *const KiwiVar,
    pub coefficient: c_double,
}

#[repr(C)]
#[derive(Debug)]
pub struct KiwiExpression<T: ?Sized = [KiwiTerm]> {
    pub constant: c_double,
    pub term_count: c_int,
    pub owner: *mut c_void,
    pub terms: T,
}

impl KiwiExpression {
    pub(crate) unsafe fn from_raw<'a>(e: *const KiwiExpressionPtr) -> &'a Self {
        &*(core::slice::from_raw_parts(e as *const (), (*e).term_count.max(0) as usize)
            as *const [()] as *const KiwiExpression)
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_expression_retain(e: *mut KiwiExpressionPtr) {
    if let Some(e) = not_null(e) {
        let e = core::slice::from_raw_parts_mut(e as *mut (), (*e).term_count.max(0) as usize)
            as *mut [()] as *mut KiwiExpression;

        (*e).terms.iter().for_each(|t| {
            if let Some(var) = not_null(t.var) {
                Rc::increment_strong_count(var);
            }
        });
        (*e).owner = e as *mut c_void;
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_expression_destroy(e: *mut KiwiExpressionPtr) {
    if let Some(e) = not_null_mut(e) {
        let e = core::slice::from_raw_parts_mut(e as *mut (), (*e).term_count.max(0) as usize)
            as *mut [()] as *mut KiwiExpression;

        if (*e).owner == e as *mut c_void {
            (*e).terms.iter().for_each(|t| {
                if let Some(var) = not_null(t.var) {
                    Rc::decrement_strong_count(var);
                }
            });
        }
    }
}
