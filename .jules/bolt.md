## 2024-05-24 - [ExAllocatePool2 redundant zeroing]
**Learning:** ExAllocatePool2 defaults to zeroing memory (e.g. POOL_FLAG_NON_PAGED). If the buffer is immediately fully overwritten (like with RtlCopyMemory), this zeroing wastes CPU/Memory bandwidth.
**Action:** Add POOL_FLAG_UNINITIALIZED when using ExAllocatePool2 if the buffer will be immediately overwritten.
