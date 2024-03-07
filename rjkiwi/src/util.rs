pub(crate) fn not_null<T: ?Sized>(ptr: *const T) -> Option<*const T> {
    if ptr.is_null() {
        None
    } else {
        Some(ptr)
    }
}

pub(crate) fn not_null_mut<T: ?Sized>(ptr: *mut T) -> Option<*mut T> {
    if ptr.is_null() {
        None
    } else {
        Some(ptr)
    }
}

pub(crate) unsafe fn not_null_ref<'a, T>(ptr: *const T) -> Option<&'a T>
where
    T: ?Sized,
{
    if ptr.is_null() {
        None
    } else {
        Some(&*ptr)
    }
}

pub(crate) unsafe fn not_null_ref_mut<'a, T>(ptr: *mut T) -> Option<&'a mut T>
where
    T: ?Sized,
{
    if ptr.is_null() {
        None
    } else {
        Some(&mut *ptr)
    }
}
