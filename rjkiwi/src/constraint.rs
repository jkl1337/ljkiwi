use std::{
    ffi::{c_double, c_int, c_uint, c_void},
    ptr::{self, drop_in_place},
};

use casuarius::{Constraint, Expression, RelationalOperator, Term};

use crate::{
    expr::{KiwiExpression, KiwiExpressionPtr},
    util::*,
    var::SolverVariable,
};

pub type KiwiConstraint = Constraint<SolverVariable>;

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq, Hash, PartialOrd, Ord, Debug)]
pub enum KiwiRelOp {
    LE,
    GE,
    EQ,
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_type_info(sz_align: *mut c_uint) {
    *sz_align.offset(0) = std::mem::size_of::<KiwiConstraint>() as c_uint;
    *sz_align.offset(1) = std::mem::align_of::<KiwiConstraint>() as c_uint;
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_init(
    c: *mut KiwiConstraint,
    lhs: *const KiwiExpressionPtr,
    rhs: *const KiwiExpressionPtr,
    op: KiwiRelOp,
    strength: f64,
) {
    let term_count = (if !lhs.is_null() {
        (*lhs).term_count.max(0)
    } else {
        0
    } + if !rhs.is_null() {
        (*rhs).term_count.max(0)
    } else {
        0
    }) as usize;

    let mut terms = Vec::<Term<SolverVariable>>::new();
    terms.reserve(term_count);

    let mut constant = 0.0;

    if !lhs.is_null() {
        let lhs = KiwiExpression::from_raw(lhs);
        terms.extend(
            lhs.terms
                .iter()
                .filter_map(|t| match SolverVariable::try_from_raw(t.var) {
                    Some(var) => Some(Term::new(var, t.coefficient)),
                    None => None,
                }),
        );
        constant += lhs.constant;
    }
    if !rhs.is_null() {
        let rhs = KiwiExpression::from_raw(rhs);
        terms.extend(
            rhs.terms
                .iter()
                .filter_map(|t| match SolverVariable::try_from_raw(t.var) {
                    Some(var) => Some(Term::new(var, -t.coefficient)),
                    None => None,
                }),
        );
        constant -= rhs.constant;
    }

    ptr::write(
        c,
        Constraint::new(
            Expression::new(terms, constant),
            match op {
                KiwiRelOp::LE => RelationalOperator::LessOrEqual,
                KiwiRelOp::GE => RelationalOperator::GreaterOrEqual,
                KiwiRelOp::EQ => RelationalOperator::Equal,
            },
            strength,
        ),
    );
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_destroy(c: *mut KiwiConstraint) {
    if let Some(c) = not_null_mut(c) {
        drop_in_place(c);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_strength(c: *const KiwiConstraint) -> c_double {
    match not_null_ref(c) {
        Some(c) => c.strength(),
        None => c_double::NAN,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_op(c: *const KiwiConstraint) -> KiwiRelOp {
    match not_null_ref(c) {
        Some(c) => match c.op() {
            RelationalOperator::LessOrEqual => KiwiRelOp::LE,
            RelationalOperator::GreaterOrEqual => KiwiRelOp::GE,
            RelationalOperator::Equal => KiwiRelOp::EQ,
        },
        None => KiwiRelOp::EQ,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_violated(c: *const KiwiConstraint) -> bool {
    const EPSILON: f64 = 1.0e-8;

    match not_null_ref(c) {
        Some(c) => {
            let e = c.expr();
            let value = e
                .terms
                .iter()
                .map(|t| t.variable.get_value() * t.coefficient.into_inner())
                .sum::<f64>()
                + e.constant.into_inner();
            match c.op() {
                RelationalOperator::LessOrEqual => value > EPSILON,
                RelationalOperator::GreaterOrEqual => value < EPSILON,
                RelationalOperator::Equal => value.abs() > EPSILON,
            }
        }
        None => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_constraint_expression(
    c: *const KiwiConstraint,
    out: *mut KiwiExpressionPtr,
    out_size: c_int,
) -> c_int {
    match not_null_ref(c) {
        Some(c) => {
            let expr = c.expr();
            let n_terms = expr.terms.len().min(c_int::MAX as usize) as c_int;
            if out.is_null() || out_size < n_terms {
                return n_terms;
            }
            let out = core::slice::from_raw_parts_mut(out as *mut (), n_terms as usize)
                as *mut [()] as *mut KiwiExpression;

            (*out).owner = out as *mut c_void;
            (*out).term_count = n_terms;
            (*out).constant = expr.constant.into();
            for (o, i) in (*out).terms.iter_mut().zip(expr.terms.iter()) {
                o.var = i.variable.retain_raw();
                o.coefficient = i.coefficient.into();
            }
            n_terms
        }
        None => 0,
    }
}
