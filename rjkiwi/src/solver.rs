use core::ffi::{c_char, c_double, c_uint};

use std::{
    ffi::CString,
    ptr::{self, drop_in_place},
};

use casuarius::{
    AddConstraintError, AddEditVariableError, RemoveConstraintError, RemoveEditVariableError,
    Solver, SuggestValueError,
};

use crate::{
    expr::{KiwiConstraint, KiwiConstraintPtr},
    util::*,
    var::{KiwiVar, SolverVariable},
};

#[repr(C)]
#[derive(Debug)]
pub enum KiwiErrKind {
    KiwiErrUnsatisfiableConstraint = 1,
    KiwiErrUnknownConstraint,
    KiwiErrDuplicateConstraint,
    KiwiErrUnknownEditVar,
    KiwiErrDuplicateEditVar,
    KiwiErrBadRequiredStrength,
    KiwiErrInternalSolverError,
    KiwiErrNullObject = 9,
}

#[repr(C)]
#[derive(Debug)]
pub struct KiwiErr {
    kind: KiwiErrKind,
    message: *mut c_char,
    must_release: bool,
}

impl KiwiErr {
    const fn new_static(kind: KiwiErrKind) -> Self {
        KiwiErr {
            kind,
            message: ptr::null_mut(),
            must_release: false,
        }
    }
    fn new_boxed(kind: KiwiErrKind, message: &str) -> *mut Self {
        Box::into_raw(Box::new(KiwiErr {
            kind,
            message: CString::new(message).unwrap().into_raw(),
            must_release: true,
        }))
    }
}

// SAFETY: pointer is for C compatibility, it is only ever null statically.
unsafe impl Sync for KiwiErr {}

impl Drop for KiwiErr {
    fn drop(&mut self) {
        if self.must_release && !self.message.is_null() {
            unsafe { drop(CString::from_raw(self.message)) }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_err_release(e: *mut KiwiErr) {
    if let Some(e) = not_null_mut(e) {
        if (*e).must_release {
            drop(Box::from_raw(e));
        }
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct KiwiSolver {
    error_mask: c_uint,
    solver: Solver<SolverVariable>,
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_type_layout(sz_align: *mut c_uint) {
    *sz_align.offset(0) = core::mem::size_of::<KiwiSolver>() as c_uint;
    *sz_align.offset(1) = core::mem::align_of::<KiwiSolver>() as c_uint;
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_init(s: *mut KiwiSolver, error_mask: c_uint) {
    ptr::write(
        s,
        KiwiSolver {
            error_mask,
            solver: Solver::default(),
        },
    );
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_destroy(s: *mut KiwiSolver) {
    if let Some(s) = not_null_mut(s) {
        drop_in_place(s);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_get_error_mask(s: *const KiwiSolver) -> c_uint {
    match not_null_ref(s) {
        Some(s) => s.error_mask,
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_set_error_mask(s: *mut KiwiSolver, mask: c_uint) {
    if let Some(s) = not_null_ref_mut(s) {
        s.error_mask = mask;
    }
}

static NULL_OBJECT_ERR: KiwiErr = KiwiErr::new_static(KiwiErrKind::KiwiErrNullObject);
static UNSATISFIABLE_CONSTRAINT_ERR: KiwiErr =
    KiwiErr::new_static(KiwiErrKind::KiwiErrUnsatisfiableConstraint);
static UNKNOWN_CONSTRAINT_ERROR: KiwiErr =
    KiwiErr::new_static(KiwiErrKind::KiwiErrUnknownConstraint);
static DUPLICATE_CONSTRAINT_ERROR: KiwiErr =
    KiwiErr::new_static(KiwiErrKind::KiwiErrDuplicateConstraint);
static UNKNOWN_EDIT_VAR_ERROR: KiwiErr = KiwiErr::new_static(KiwiErrKind::KiwiErrUnknownEditVar);
static DUPLICATE_EDIT_VAR_ERROR: KiwiErr =
    KiwiErr::new_static(KiwiErrKind::KiwiErrDuplicateEditVar);
static BAD_REQUIRED_STRENGTH_ERROR: KiwiErr =
    KiwiErr::new_static(KiwiErrKind::KiwiErrBadRequiredStrength);

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_add_constraint(
    s: *mut KiwiSolver,
    constraint: *const KiwiConstraintPtr,
) -> *const KiwiErr {
    use AddConstraintError::*;

    let (Some(s), Some(constraint)) = (
        not_null_ref_mut(s),
        KiwiConstraint::try_construct(constraint),
    ) else {
        return &NULL_OBJECT_ERR;
    };

    match s.solver.add_constraint(constraint) {
        Ok(_) => ptr::null(),
        Err(err) => match err {
            DuplicateConstraint => &DUPLICATE_CONSTRAINT_ERROR,
            UnsatisfiableConstraint => &UNSATISFIABLE_CONSTRAINT_ERR,
            InternalSolverError(m) => {
                KiwiErr::new_boxed(KiwiErrKind::KiwiErrInternalSolverError, m)
            }
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_remove_constraint(
    s: *mut KiwiSolver,
    constraint: *const KiwiConstraintPtr,
) -> *const KiwiErr {
    use RemoveConstraintError::*;

    let (Some(s), Some(constraint)) = (
        not_null_ref_mut(s),
        KiwiConstraint::try_construct(constraint),
    ) else {
        return &NULL_OBJECT_ERR;
    };
    match s.solver.remove_constraint(&constraint) {
        Ok(_) => ptr::null(),
        Err(err) => match err {
            UnknownConstraint => &UNKNOWN_CONSTRAINT_ERROR,
            InternalSolverError(m) => {
                KiwiErr::new_boxed(KiwiErrKind::KiwiErrInternalSolverError, m)
            }
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_add_edit_var(
    s: *mut KiwiSolver,
    var: *const KiwiVar,
    strength: c_double,
) -> *const KiwiErr {
    use AddEditVariableError::*;

    let (Some(s), Some(var)) = (not_null_ref_mut(s), SolverVariable::try_clone_raw(var)) else {
        return &NULL_OBJECT_ERR;
    };
    match s.solver.add_edit_variable(var, strength) {
        Ok(_) => ptr::null(),
        Err(err) => match err {
            DuplicateEditVariable => &DUPLICATE_EDIT_VAR_ERROR,
            BadRequiredStrength => &BAD_REQUIRED_STRENGTH_ERROR,
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_remove_edit_var(
    s: *mut KiwiSolver,
    var: *const KiwiVar,
) -> *const KiwiErr {
    use RemoveEditVariableError::*;

    let (Some(s), Some(var)) = (not_null_ref_mut(s), SolverVariable::try_clone_raw(var)) else {
        return &NULL_OBJECT_ERR;
    };

    match s.solver.remove_edit_variable(var) {
        Ok(_) => ptr::null(),
        Err(err) => match err {
            UnknownEditVariable => &UNKNOWN_EDIT_VAR_ERROR,
            InternalSolverError(m) => {
                KiwiErr::new_boxed(KiwiErrKind::KiwiErrInternalSolverError, m)
            }
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_suggest_value(
    s: *mut KiwiSolver,
    var: *const KiwiVar,
    value: c_double,
) -> *const KiwiErr {
    use SuggestValueError::*;

    let (Some(s), Some(var)) = (not_null_ref_mut(s), SolverVariable::try_clone_raw(var)) else {
        return &NULL_OBJECT_ERR;
    };

    match s.solver.suggest_value(var, value) {
        Ok(_) => ptr::null(),
        Err(err) => match err {
            UnknownEditVariable => &UNKNOWN_EDIT_VAR_ERROR,
            InternalSolverError(m) => {
                KiwiErr::new_boxed(KiwiErrKind::KiwiErrInternalSolverError, m)
            }
        },
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_has_edit_var(
    s: *const KiwiSolver,
    var: *const KiwiVar,
) -> bool {
    match (not_null_ref(s), SolverVariable::try_clone_raw(var)) {
        (Some(s), Some(var)) => s.solver.has_edit_variable(&var),
        _ => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_has_constraint(
    s: *const KiwiSolver,
    constraint: *const KiwiConstraintPtr,
) -> bool {
    match (not_null_ref(s), KiwiConstraint::try_construct(constraint)) {
        (Some(s), Some(constraint)) => s.solver.has_constraint(&constraint),
        _ => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_update_vars(s: *mut KiwiSolver) {
    if let Some(s) = not_null_ref_mut(s) {
        s.solver
            .fetch_changes()
            .iter()
            .for_each(|(v, val)| v.set_value(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_reset(s: *mut KiwiSolver) {
    if let Some(s) = not_null_ref_mut(s) {
        s.solver.reset();
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_dump(s: *const KiwiSolver) {
    if let Some(s) = not_null_ref(s) {
        println!("{:?}", s.solver);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_solver_dumps(s: *const KiwiSolver) -> *mut c_char {
    match not_null_ref(s) {
        Some(s) => match CString::new(format!("{:?}", s.solver)) {
            Ok(s) => s.into_raw(),
            Err(_) => ptr::null_mut(),
        },
        None => ptr::null_mut(),
    }
}
