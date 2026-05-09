// ═══════════════════════════════════════════════════════════════════════
// shm_ring_writer.rs — UI-002: Rust SHM Ring Buffer Writer
//
// Implements the host-side writer for the 4GB SHM Neural Bus ring buffer
// (8 × 512MB slots per §VII·7). CRC64 integrity, atomic slot state
// transitions per §XVIII·3.
//
// Slot states: FREE(0) → WRITING(1) → COMMITTED(2) → READING(3) → FREE(0)
//
// Reference: Interactive_Plan.md §XI UI-002, §XVIII·3
// ═══════════════════════════════════════════════════════════════════════

use std::ptr;
use std::sync::atomic::{AtomicU32, Ordering};

/// 512 MB per slot
const SLOT_SIZE: usize = 512 * 1024 * 1024;
/// 8 slots = 4 GB total
const SLOT_COUNT: usize = 8;
/// Total SHM size
const SHM_TOTAL_SIZE: usize = SLOT_SIZE * SLOT_COUNT;
/// SHM object name (must match guest reader)
const SHM_NAME: &str = "SymbioseOS_NeuralBus_SHM";

/// Slot header (64 bytes, at the start of each 512MB slot)
#[repr(C)]
#[derive(Debug)]
pub struct SlotHeader {
    /// Atomic state: 0=FREE, 1=WRITING, 2=COMMITTED, 3=READING
    pub state: AtomicU32,
    /// Payload type (modality ID from modality_router.c)
    pub payload_type: u32,
    /// Payload length in bytes (excluding header)
    pub payload_len: u64,
    /// CRC64 of payload data
    pub crc64: u64,
    /// Nanosecond timestamp (host monotonic clock)
    pub timestamp_ns: u64,
    /// Sequence number (monotonically increasing)
    pub sequence: u64,
    /// Reserved for alignment (pad to 64 bytes)
    pub _reserved: [u8; 16],
}

/// Slot states
const SLOT_FREE: u32 = 0;
const SLOT_WRITING: u32 = 1;
const SLOT_COMMITTED: u32 = 2;
#[allow(dead_code)]
const SLOT_READING: u32 = 3;

/// CRC64-ECMA table (precomputed)
const CRC64_POLY: u64 = 0x42F0E1EBA9EA3693;

fn crc64_init_table() -> [u64; 256] {
    let mut table = [0u64; 256];
    for i in 0..256 {
        let mut crc = i as u64;
        for _ in 0..8 {
            if crc & 1 == 1 {
                crc = (crc >> 1) ^ CRC64_POLY;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    table
}

fn crc64_compute(data: &[u8]) -> u64 {
    let table = crc64_init_table();
    let mut crc: u64 = 0xFFFFFFFFFFFFFFFF;
    for &byte in data {
        let idx = ((crc ^ byte as u64) & 0xFF) as usize;
        crc = (crc >> 8) ^ table[idx];
    }
    crc ^ 0xFFFFFFFFFFFFFFFF
}

/// SHM Ring Buffer handle
pub struct ShmRingWriter {
    /// Base pointer to mapped SHM region
    base_ptr: *mut u8,
    /// Windows file mapping handle
    #[cfg(target_os = "windows")]
    map_handle: *mut std::ffi::c_void,
    /// Monotonic sequence counter
    next_sequence: u64,
}

// Safety: SHM is process-local memory-mapped, atomic ops handle concurrency
unsafe impl Send for ShmRingWriter {}
unsafe impl Sync for ShmRingWriter {}

impl ShmRingWriter {
    /// Open or create the SHM ring buffer.
    ///
    /// Uses Windows `OpenFileMappingA` to attach to an existing SHM region
    /// created by the KMDF driver (symbiose_bridge.sys) or ChaosLoader.exe.
    #[cfg(target_os = "windows")]
    pub fn open() -> Result<Self, String> {
        use std::ffi::CString;

        let name = CString::new(SHM_NAME).unwrap();

        unsafe {
            // Try to open existing mapping first
            let handle = OpenFileMappingA(
                FILE_MAP_ALL_ACCESS,
                0, // bInheritHandle = FALSE
                name.as_ptr(),
            );

            if handle.is_null() {
                // Create new mapping (for development/testing)
                let handle = CreateFileMappingA(
                    INVALID_HANDLE_VALUE,
                    ptr::null_mut(),
                    PAGE_READWRITE,
                    (SHM_TOTAL_SIZE >> 32) as u32,
                    SHM_TOTAL_SIZE as u32,
                    name.as_ptr(),
                );

                if handle.is_null() {
                    return Err(format!(
                        "Failed to create SHM: error {}",
                        GetLastError()
                    ));
                }

                let ptr = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, SHM_TOTAL_SIZE);
                if ptr.is_null() {
                    CloseHandle(handle);
                    return Err(format!(
                        "Failed to map SHM: error {}",
                        GetLastError()
                    ));
                }

                // Zero-initialize all slots (mark as FREE)
                ptr::write_bytes(ptr as *mut u8, 0, SHM_TOTAL_SIZE);

                Ok(Self {
                    base_ptr: ptr as *mut u8,
                    map_handle: handle,
                    next_sequence: 0,
                })
            } else {
                let ptr = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, SHM_TOTAL_SIZE);
                if ptr.is_null() {
                    CloseHandle(handle);
                    return Err(format!(
                        "Failed to map SHM: error {}",
                        GetLastError()
                    ));
                }

                Ok(Self {
                    base_ptr: ptr as *mut u8,
                    map_handle: handle,
                    next_sequence: 0,
                })
            }
        }
    }

    /// Stub for non-Windows builds (tests)
    #[cfg(not(target_os = "windows"))]
    pub fn open() -> Result<Self, String> {
        let layout = std::alloc::Layout::from_size_align(SHM_TOTAL_SIZE, 4096)
            .map_err(|e| format!("Layout error: {}", e))?;
        let ptr = unsafe { std::alloc::alloc_zeroed(layout) };
        if ptr.is_null() {
            return Err("Failed to allocate SHM stub".into());
        }
        Ok(Self {
            base_ptr: ptr,
            next_sequence: 0,
        })
    }

    /// Get pointer to slot N header
    fn slot_header(&self, slot: usize) -> &SlotHeader {
        assert!(slot < SLOT_COUNT);
        unsafe {
            let ptr = self.base_ptr.add(slot * SLOT_SIZE);
            &*(ptr as *const SlotHeader)
        }
    }

    /// Get mutable pointer to slot N data region (after 64-byte header)
    fn slot_data_ptr(&self, slot: usize) -> *mut u8 {
        assert!(slot < SLOT_COUNT);
        unsafe {
            self.base_ptr
                .add(slot * SLOT_SIZE)
                .add(std::mem::size_of::<SlotHeader>())
        }
    }

    /// Maximum payload size per slot (512MB - 64B header)
    pub fn max_payload_size() -> usize {
        SLOT_SIZE - std::mem::size_of::<SlotHeader>()
    }

    /// Acquire a free slot for writing.
    ///
    /// Scans all 8 slots looking for state=FREE, atomically transitions to WRITING.
    /// Returns slot index or None if all slots are busy.
    pub fn ring_acquire_write(&self) -> Option<usize> {
        for slot in 0..SLOT_COUNT {
            let header = self.slot_header(slot);
            // Atomic CAS: FREE → WRITING
            if header
                .state
                .compare_exchange(SLOT_FREE, SLOT_WRITING, Ordering::AcqRel, Ordering::Relaxed)
                .is_ok()
            {
                return Some(slot);
            }
        }
        None // All slots busy
    }

    /// Write data to an acquired slot and commit it.
    ///
    /// Computes CRC64, writes header fields, copies payload data,
    /// then atomically transitions state WRITING → COMMITTED.
    pub fn ring_write_and_commit(
        &mut self,
        slot: usize,
        payload_type: u32,
        data: &[u8],
    ) -> Result<(), String> {
        assert!(slot < SLOT_COUNT);

        if data.len() > Self::max_payload_size() {
            return Err(format!(
                "Payload too large: {} bytes (max {})",
                data.len(),
                Self::max_payload_size()
            ));
        }

        // Verify we own this slot (state = WRITING)
        {
            let header = self.slot_header(slot);
            let current = header.state.load(Ordering::Acquire);
            if current != SLOT_WRITING {
                return Err(format!(
                    "Slot {} not in WRITING state (state={})",
                    slot, current
                ));
            }
        }

        // Compute CRC64
        let crc = crc64_compute(data);

        // Get timestamp
        let timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos() as u64;

        // Write payload data first (before header — prevents partial reads)
        unsafe {
            let data_ptr = self.slot_data_ptr(slot);
            ptr::copy_nonoverlapping(data.as_ptr(), data_ptr, data.len());
        }

        // Write header fields (except state — that's last)
        // We use raw pointer writes since the header is in SHM
        let seq = self.next_sequence;
        unsafe {
            let hdr_ptr = self.base_ptr.add(slot * SLOT_SIZE) as *mut SlotHeader;
            (*hdr_ptr).payload_type = payload_type;
            (*hdr_ptr).payload_len = data.len() as u64;
            (*hdr_ptr).crc64 = crc;
            (*hdr_ptr).timestamp_ns = timestamp;
            (*hdr_ptr).sequence = seq;
        }

        self.next_sequence += 1;

        // Memory fence before state transition
        std::sync::atomic::fence(Ordering::Release);

        // Atomic: WRITING → COMMITTED (re-borrow header after mutation)
        let header = self.slot_header(slot);
        header.state.store(SLOT_COMMITTED, Ordering::Release);

        Ok(())
    }

    /// Abort a write — release slot back to FREE without committing.
    pub fn ring_abort_write(&self, slot: usize) {
        assert!(slot < SLOT_COUNT);
        let header = self.slot_header(slot);
        header.state.store(SLOT_FREE, Ordering::Release);
    }

    /// Get status of all 8 slots (for telemetry)
    pub fn slot_states(&self) -> [u32; SLOT_COUNT] {
        let mut states = [0u32; SLOT_COUNT];
        for i in 0..SLOT_COUNT {
            states[i] = self.slot_header(i).state.load(Ordering::Relaxed);
        }
        states
    }

    /// Count active (non-FREE) slots
    pub fn active_slot_count(&self) -> usize {
        self.slot_states().iter().filter(|&&s| s != SLOT_FREE).count()
    }
}

impl Drop for ShmRingWriter {
    fn drop(&mut self) {
        #[cfg(target_os = "windows")]
        unsafe {
            UnmapViewOfFile(self.base_ptr as *mut std::ffi::c_void);
            CloseHandle(self.map_handle);
        }

        #[cfg(not(target_os = "windows"))]
        unsafe {
            let layout = std::alloc::Layout::from_size_align(SHM_TOTAL_SIZE, 4096).unwrap();
            std::alloc::dealloc(self.base_ptr, layout);
        }
    }
}

// ── Windows FFI ──────────────────────────────────────────────────────
#[cfg(target_os = "windows")]
const FILE_MAP_ALL_ACCESS: u32 = 0xF001F;
#[cfg(target_os = "windows")]
const PAGE_READWRITE: u32 = 0x04;
#[cfg(target_os = "windows")]
const INVALID_HANDLE_VALUE: *mut std::ffi::c_void = -1isize as *mut std::ffi::c_void;

#[cfg(target_os = "windows")]
extern "system" {
    fn OpenFileMappingA(
        access: u32,
        inherit: i32,
        name: *const i8,
    ) -> *mut std::ffi::c_void;

    fn CreateFileMappingA(
        file: *mut std::ffi::c_void,
        attrs: *mut std::ffi::c_void,
        protect: u32,
        size_high: u32,
        size_low: u32,
        name: *const i8,
    ) -> *mut std::ffi::c_void;

    fn MapViewOfFile(
        mapping: *mut std::ffi::c_void,
        access: u32,
        offset_high: u32,
        offset_low: u32,
        bytes: usize,
    ) -> *mut std::ffi::c_void;

    fn UnmapViewOfFile(base: *mut std::ffi::c_void) -> i32;
    fn CloseHandle(handle: *mut std::ffi::c_void) -> i32;
    fn GetLastError() -> u32;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc64() {
        let data = b"Hello, SymbioseOS!";
        let crc = crc64_compute(data);
        assert_ne!(crc, 0);
        // Same data → same CRC
        assert_eq!(crc, crc64_compute(data));
        // Different data → different CRC
        assert_ne!(crc, crc64_compute(b"Different"));
    }

    #[test]
    fn test_ring_acquire_and_commit() {
        let mut ring = ShmRingWriter::open().expect("Failed to open stub SHM");

        // Acquire slot
        let slot = ring.ring_acquire_write().expect("Should get a free slot");
        assert!(slot < SLOT_COUNT);

        // Write and commit
        let payload = vec![0x42u8; 1024];
        ring.ring_write_and_commit(slot, 1, &payload)
            .expect("Write should succeed");

        // Slot should be COMMITTED
        let header = ring.slot_header(slot);
        assert_eq!(header.state.load(Ordering::Relaxed), SLOT_COMMITTED);
        assert_eq!(header.payload_len as usize, 1024);
        assert_ne!(header.crc64, 0);
    }

    #[test]
    fn test_all_slots_busy() {
        let ring = ShmRingWriter::open().expect("Failed to open stub SHM");

        // Acquire all 8 slots
        for _ in 0..SLOT_COUNT {
            ring.ring_acquire_write().expect("Should get slot");
        }

        // 9th acquire should fail
        assert!(ring.ring_acquire_write().is_none());
    }
}
