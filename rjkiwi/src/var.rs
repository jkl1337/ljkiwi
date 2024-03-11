use core::{
    ffi::{c_char, c_double, CStr},
    fmt,
};
use std::{
    alloc::{alloc, dealloc, handle_alloc_error, Layout},
    cell::{Cell, UnsafeCell},
    ffi::CString,
    hash::{Hash, Hasher},
    marker::PhantomData,
    ptr::{self, drop_in_place, NonNull},
};

#[repr(C)]
pub union KiwiVarNameData {
    ptr: *mut c_char,
    inline: [u8; 16],
}

#[repr(C)]
pub struct KiwiVarName {
    pub len: usize,
    pub data: KiwiVarNameData,
}

impl KiwiVarName {
    fn new(name: &CStr) -> Self {
        let len = name.to_bytes_with_nul().len() - 1;
        KiwiVarName {
            len,
            data: if len > 15 {
                KiwiVarNameData {
                    ptr: name.to_owned().into_raw(),
                }
            } else {
                let mut data = KiwiVarNameData { inline: [0; 16] };
                unsafe { data.inline[..len + 1].copy_from_slice(name.to_bytes_with_nul()) };
                data
            },
        }
    }

    fn as_cstr(&self) -> &CStr {
        unsafe {
            if self.len > 15 {
                CStr::from_ptr(self.data.ptr)
            } else {
                CStr::from_bytes_with_nul_unchecked(&self.data.inline[..=self.len + 1])
            }
        }
    }

    fn set(&mut self, name: &CStr) {
        if self.len > 15 {
            unsafe {
                drop(CString::from_raw(self.data.ptr));
            }
        }
        self.len = name.to_bytes_with_nul().len() - 1;
        unsafe {
            if self.len > 15 {
                self.data.ptr = name.to_owned().into_raw();
            } else {
                self.data.inline[..self.len + 1].copy_from_slice(name.to_bytes_with_nul());
            }
        }
    }
}

impl Drop for KiwiVarName {
    fn drop(&mut self) {
        if self.len > 15 {
            unsafe {
                drop(CString::from_raw(self.data.ptr));
            }
        }
    }
}

#[repr(C)]
pub struct KiwiVar {
    pub ref_count_: Cell<usize>,
    pub value_: Cell<c_double>,
    pub name_: UnsafeCell<KiwiVarName>,
}

impl fmt::Debug for KiwiVar {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("KiwiVar")
            .field("ref_count", &self.ref_count_.get())
            .field("value", &self.value_.get())
            .field("name", &self.name())
            .finish()
    }
}

impl KiwiVar {
    #[inline]
    fn inc_count(&self) {
        self.ref_count_.set(self.ref_count_.get().wrapping_add(1));
    }

    #[inline]
    fn dec_count(&self) -> bool {
        let count = self.ref_count_.get().wrapping_sub(1);
        self.ref_count_.set(count);

        count == 0
    }

    #[inline]
    pub(crate) fn value(&self) -> c_double {
        self.value_.get()
    }

    #[inline]
    pub(crate) fn name(&self) -> String {
        let name = unsafe { &*self.name_.get() };
        name.as_cstr().to_string_lossy().into_owned()
    }

    #[inline]
    pub(crate) unsafe fn retain_raw(var: *const Self) -> *const Self {
        if let Some(var) = NonNull::new(var as *mut Self) {
            var.as_ref().inc_count();
        }
        var
    }
}

pub(crate) struct SolverVariable {
    ptr: NonNull<KiwiVar>,
    phantom: PhantomData<KiwiVar>,
}

impl SolverVariable {
    #[inline]
    fn inner(&self) -> &KiwiVar {
        unsafe { self.ptr.as_ref() }
    }

    #[inline]
    pub(crate) fn set_value(&self, value: &f64) {
        self.inner().value_.set(*value);
    }

    #[inline]
    unsafe fn from_raw(ptr: *const KiwiVar) -> Self {
        let ptr = NonNull::new_unchecked(ptr as *mut KiwiVar);
        SolverVariable {
            ptr,
            phantom: PhantomData,
        }
    }

    #[inline]
    pub(crate) unsafe fn clone_raw(ptr: *const KiwiVar) -> Self {
        let v = Self::from_raw(ptr);
        v.inner().inc_count();
        v
    }

    #[inline]
    pub(crate) unsafe fn try_clone_raw(ptr: *const KiwiVar) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            Some(Self::clone_raw(ptr))
        }
    }

    #[inline]
    pub(crate) unsafe fn drop_raw(ptr: *const KiwiVar) {
        if !ptr.is_null() {
            drop(Self::from_raw(ptr));
        }
    }

    unsafe fn destroy_inner(ptr: *mut KiwiVar) {
        drop_in_place(ptr);
        dealloc(ptr as *mut u8, Layout::new::<KiwiVar>());
    }
}

impl Clone for SolverVariable {
    fn clone(&self) -> Self {
        self.inner().inc_count();
        SolverVariable {
            ptr: self.ptr,
            phantom: PhantomData,
        }
    }
}

impl Drop for SolverVariable {
    fn drop(&mut self) {
        if self.inner().dec_count() {
            unsafe { SolverVariable::destroy_inner(self.ptr.as_ptr()) }
        }
    }
}

impl Eq for SolverVariable {}

impl PartialEq for SolverVariable {
    fn eq(&self, other: &Self) -> bool {
        ptr::addr_eq(self.ptr.as_ptr(), other.ptr.as_ptr())
    }
}

impl Hash for SolverVariable {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.ptr.as_ptr().hash(state);
    }
}

impl fmt::Debug for SolverVariable {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("SolverVariable")
            .field(&self.inner().name())
            .finish()
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_new(name: *const c_char) -> *const KiwiVar {
    let name = match NonNull::new(name as *mut c_char) {
        Some(name) => CStr::from_ptr(name.as_ptr()),
        None => CStr::from_bytes_with_nul_unchecked(b"\0"),
    };

    let layout = Layout::new::<KiwiVar>();
    let p = alloc(layout) as *mut KiwiVar;
    if p.is_null() {
        handle_alloc_error(layout);
    }
    ptr::write(
        p,
        KiwiVar {
            ref_count_: Cell::new(1),
            value_: Cell::new(0.0),
            name_: UnsafeCell::new(KiwiVarName::new(name)),
        },
    );
    p
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_free(var: *const KiwiVar) {
    if let Some(var) = NonNull::new(var as *mut KiwiVar) {
        SolverVariable::destroy_inner(var.as_ptr());
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_value(var: *const KiwiVar) -> c_double {
    match NonNull::new(var as *mut KiwiVar) {
        Some(var) => var.as_ref().value_.get(),
        None => c_double::NAN,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_set_value(var: *const KiwiVar, value: c_double) {
    if let Some(var) = NonNull::new(var as *mut KiwiVar) {
        var.as_ref().value_.set(value);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_name(var: *const KiwiVar) -> *const c_char {
    match NonNull::new(var as *mut KiwiVar) {
        Some(var) => unsafe { &*var.as_ref().name_.get() }.as_cstr().as_ptr(),
        None => CStr::from_bytes_with_nul_unchecked(b"\0").as_ptr(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kiwi_var_set_name(var: *const KiwiVar, name: *const c_char) {
    if let Some(var) = NonNull::new(var as *mut KiwiVar) {
        let var_name = &mut *var.as_ref().name_.get();

        var_name.set(match NonNull::new(name as *mut c_char) {
            Some(name) => CStr::from_ptr(name.as_ptr()),
            None => CStr::from_bytes_with_nul_unchecked(b"\0"),
        });
    }
}
