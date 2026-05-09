// Newtype handles — each wraps a raw u32 with no implicit conversions.
// #[repr(transparent)] ensures ABI identity with u32 across extern "C" calls.

#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct ResourceHandle(pub u32);

#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct ImageHandle(pub u32);

#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct VoiceHandle(pub u32);

#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct HandlerHandle(pub u32);

#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq)]
pub struct Slot(pub u32);
