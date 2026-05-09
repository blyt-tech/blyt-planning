// Typed state-buffer field handles.  FieldHandle<B> is parameterised on the
// buffer marker type B so the compiler rejects cross-buffer field accesses at
// compile time.

use core::marker::PhantomData;
use crate::handles::Slot;
use crate::ffi;

mod private {
    pub trait Sealed {}
}

pub trait Buffer: private::Sealed {}

pub struct MainBuffer;
impl private::Sealed for MainBuffer {}
impl Buffer for MainBuffer {}

pub struct EnemiesBuffer;
impl private::Sealed for EnemiesBuffer {}
impl Buffer for EnemiesBuffer {}

#[derive(Copy, Clone, PartialEq, Eq)]
pub struct FieldHandle<B: Buffer>(pub u32, PhantomData<B>);

impl<B: Buffer> FieldHandle<B> {
    pub const fn new(raw: u32) -> Self {
        FieldHandle(raw, PhantomData)
    }
}

impl MainBuffer {
    #[inline(always)]
    pub fn get_u32(&self, slot: Slot, field: FieldHandle<MainBuffer>) -> u32 {
        unsafe { ffi::blyt32_state_get_u32(slot.0, field.0) }
    }

    #[inline(always)]
    pub fn set_u32(&self, slot: Slot, field: FieldHandle<MainBuffer>, val: u32) {
        unsafe { ffi::blyt32_state_set_u32(slot.0, field.0, val) }
    }
}

impl EnemiesBuffer {
    #[inline(always)]
    pub fn get_u32(&self, slot: Slot, field: FieldHandle<EnemiesBuffer>) -> u32 {
        unsafe { ffi::blyt32_state_get_u32(slot.0, field.0) }
    }

    #[inline(always)]
    pub fn set_u32(&self, slot: Slot, field: FieldHandle<EnemiesBuffer>, val: u32) {
        unsafe { ffi::blyt32_state_set_u32(slot.0, field.0, val) }
    }
}
