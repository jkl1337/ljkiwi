use std::{
    ffi::{c_double, c_int, c_void},
    rc::Rc,
};

use casuarius::{Constraint, Expression, RelationalOperator, Term};

use crate::{
    util::*,
    var::{var_retain_raw, KiwiVar, SolverVariable},
};

#[repr(C)]
#[derive(Debug)]
pub enum KiwiRelOp {
    LE,
    GE,
    EQ,
}

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
    pub terms_: T,
}

pub type KiwiExpressionPtr = KiwiExpression<[KiwiTerm; 0]>;

impl KiwiExpression {
    pub(crate) unsafe fn from_raw<'a>(e: *const KiwiExpressionPtr) -> &'a Self {
        &*(core::slice::from_raw_parts(e as *const (), (*e).term_count.max(0) as usize)
            as *const [()] as *const KiwiExpression)
    }

    pub(crate) unsafe fn try_from_raw<'a>(e: *const KiwiExpressionPtr) -> Option<&'a Self> {
        if e.is_null() {
            None
        } else {
            Some(Self::from_raw(e))
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_expression_retain(e: *mut KiwiExpressionPtr) {
    let Some(e) = not_null_mut(e) else { return };

    let e = core::slice::from_raw_parts_mut(e as *mut (), (*e).term_count.max(0) as usize)
        as *mut [()] as *mut KiwiExpression;

    (*e).terms_.iter().for_each(|t| {
        if let Some(var) = not_null(t.var) {
            Rc::increment_strong_count(var);
        }
    });
    (*e).owner = e as *mut c_void;
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_expression_destroy(e: *mut KiwiExpressionPtr) {
    let Some(e) = not_null_mut(e) else { return };

    let e = core::slice::from_raw_parts_mut(e as *mut (), (*e).term_count.max(0) as usize)
        as *mut [()] as *mut KiwiExpression;

    if (*e).owner == e as *mut c_void {
        (*e).terms_.iter().for_each(|t| {
            if let Some(var) = not_null(t.var) {
                Rc::decrement_strong_count(var);
            }
        });
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct KiwiConstraint<T: ?Sized = [KiwiTerm]> {
    pub constant: c_double,
    pub term_count: c_int,
    pub op_: KiwiRelOp,
    pub strength_: c_double,

    pub owner: *mut c_void,
    pub terms_: T,
}

pub type KiwiConstraintPtr = KiwiConstraint<[KiwiTerm; 0]>;

impl KiwiConstraint {
    pub(crate) unsafe fn from_raw<'a>(c: *const KiwiConstraintPtr) -> &'a Self {
        &*(core::slice::from_raw_parts(c as *const (), (*c).term_count.max(0) as usize)
            as *const [()] as *const KiwiConstraint)
    }

    pub(crate) unsafe fn try_from_raw<'a>(c: *const KiwiConstraintPtr) -> Option<&'a Self> {
        if c.is_null() {
            None
        } else {
            Some(Self::from_raw(c))
        }
    }
}

pub(crate) unsafe fn try_to_constraint(
    c: *const KiwiConstraintPtr,
) -> Option<Constraint<SolverVariable>> {
    let c = KiwiConstraint::try_from_raw(c)?;

    let e = Expression::new(
        c.terms_
            .iter()
            .map(|t| Term::new(SolverVariable::try_from_raw(t.var).unwrap(), t.coefficient))
            .collect(),
        c.constant,
    );

    use KiwiRelOp::*;
    use RelationalOperator::*;
    Some(Constraint::new(
        e,
        match c.op_ {
            LE => LessOrEqual,
            GE => GreaterOrEqual,
            EQ => Equal,
        },
        c.strength_,
    ))
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_init(
    c: *mut KiwiConstraintPtr,
    lhs: *const KiwiExpressionPtr,
    rhs: *const KiwiExpressionPtr,
    op: KiwiRelOp,
    strength: f64,
) {
    let Some(c) = not_null_mut(c) else { return };

    let term_space = (if !lhs.is_null() { (*lhs).term_count } else { 0 }
        + if !rhs.is_null() { (*rhs).term_count } else { 0 })
    .max(0) as usize;

    let c = core::slice::from_raw_parts_mut(c as *mut (), term_space as usize) as *mut [()]
        as *mut KiwiConstraint;

    let mut i = 0usize;
    let mut constant = 0.0;

    if let Some(lhs) = KiwiExpression::try_from_raw(lhs) {
        constant += lhs.constant;

        for t in lhs.terms_.iter().take(term_space) {
            (*c).terms_[i] = KiwiTerm {
                var: var_retain_raw(t.var),
                coefficient: t.coefficient,
            };
            i += 1;
        }
    }

    if let Some(rhs) = KiwiExpression::try_from_raw(rhs) {
        constant -= rhs.constant;

        for t in rhs.terms_.iter().take(term_space - i) {
            (*c).terms_[i] = KiwiTerm {
                var: var_retain_raw(t.var),
                coefficient: -t.coefficient,
            };
            i += 1;
        }
    }

    (*c).constant = constant;
    (*c).owner = c as *mut c_void;
    (*c).term_count = i as c_int;
    (*c).op_ = op;
    (*c).strength_ = strength;
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_destroy(c: *mut KiwiConstraintPtr) {
    let Some(c) = not_null_mut(c) else { return };

    let c = core::slice::from_raw_parts_mut(c as *mut (), (*c).term_count.max(0) as usize)
        as *mut [()] as *mut KiwiConstraint;

    if (*c).owner == c as *mut c_void {
        (*c).terms_.iter().for_each(|t| {
            if let Some(var) = not_null(t.var) {
                Rc::decrement_strong_count(var);
            }
        });
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_violated(c: *const KiwiConstraintPtr) -> bool {
    const EPSILON: f64 = 1.0e-8;

    match KiwiConstraint::try_from_raw(c) {
        Some(c) => {
            let value = c
                .terms_
                .iter()
                .map(|t| (*not_null_ref(t.var).unwrap().get()).value_ * t.coefficient)
                .sum::<f64>()
                + c.constant;
            match c.op_ {
                KiwiRelOp::LE => value > EPSILON,
                KiwiRelOp::GE => value < EPSILON,
                KiwiRelOp::EQ => value.abs() > EPSILON,
            }
        }
        None => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_expression(
    c: *const KiwiConstraintPtr,
    out: *mut KiwiExpressionPtr,
    out_size: c_int,
) -> c_int {
    let Some(c) = KiwiConstraint::try_from_raw(c) else {
        return 0;
    };

    let n_terms = c.terms_.len().min(c_int::MAX as usize) as c_int;
    if out.is_null() || out_size < n_terms {
        return n_terms;
    }

    let out = core::slice::from_raw_parts_mut(out as *mut (), n_terms as usize) as *mut [()]
        as *mut KiwiExpression;

    (*out).owner = out as *mut c_void;
    (*out).term_count = n_terms;
    (*out).constant = c.constant;

    for (o, i) in (*out).terms_.iter_mut().zip(c.terms_.iter()) {
        o.var = var_retain_raw(i.var);
        o.coefficient = i.coefficient;
    }
    n_terms
}