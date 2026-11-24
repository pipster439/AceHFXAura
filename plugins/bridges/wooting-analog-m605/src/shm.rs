use std::ffi::c_void;
use std::mem::size_of;
use std::ptr::{self, null_mut};
use std::sync::atomic::{fence, AtomicU32, Ordering};
use windows_sys::Win32::Foundation::{CloseHandle, HANDLE};
use windows_sys::Win32::System::Memory::{
    MapViewOfFile, OpenFileMappingW, UnmapViewOfFile, FILE_MAP_READ, MEMORY_MAPPED_VIEW_ADDRESS,
};

pub const HALL_KEY_COUNT: usize = 68;
pub const HALL_SHM_MAGIC: u32 = 0x414D_3635; // "AM65"
pub const HALL_SHM_VERSION: u32 = 1;
pub const HALL_SHM_NAME_WIDE: &str = "Local\\AuraM605HallAnalogV1\0";

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AuraM605HallSharedKey {
    pub hid_usage: u16,
    pub logical_id: u16,
    pub normalized: f32,
    pub depth_mm: f32,
    pub raw: u16,
    pub digital_down: u8,
    pub reserved: u8,
}

#[repr(C)]
pub struct AuraM605HallSharedMemory {
    pub magic: u32,
    pub version: u32,
    pub seq: AtomicU32,
    pub active: u32,
    pub timestamp_us: u64,
    pub packet_count: u32,
    pub key_count: u32,
    pub vid: u16,
    pub pid: u16,
    pub reserved: u32,
    pub keys: [AuraM605HallSharedKey; HALL_KEY_COUNT],
}

#[derive(Clone, Debug)]
pub struct SharedSnapshot {
    pub active: bool,
    pub timestamp_us: u64,
    pub packet_count: u32,
    pub vid: u16,
    pub pid: u16,
    pub keys: [AuraM605HallSharedKey; HALL_KEY_COUNT],
}

pub struct ShmReader {
    handle: HANDLE,
    view: *const AuraM605HallSharedMemory,
}

// Handle and read-only mapped view remain valid across threads until unmapped in Drop.
unsafe impl Send for ShmReader {}

impl ShmReader {
    pub fn new() -> Self {
        let mut r = Self {
            handle: null_mut(),
            view: ptr::null(),
        };
        let _ = r.try_open();
        r
    }

    pub fn try_open(&mut self) -> bool {
        if !self.view.is_null() {
            return true;
        }
        let wide: Vec<u16> = HALL_SHM_NAME_WIDE.encode_utf16().collect();
        unsafe {
            let h = OpenFileMappingW(FILE_MAP_READ, 0, wide.as_ptr());
            if h.is_null() {
                return false;
            }
            let addr: MEMORY_MAPPED_VIEW_ADDRESS =
                MapViewOfFile(h, FILE_MAP_READ, 0, 0, size_of::<AuraM605HallSharedMemory>());
            if addr.Value.is_null() {
                CloseHandle(h);
                return false;
            }
            self.handle = h;
            self.view = addr.Value as *const AuraM605HallSharedMemory;
        }
        true
    }

    pub fn close(&mut self) {
        unsafe {
            if !self.view.is_null() {
                UnmapViewOfFile(MEMORY_MAPPED_VIEW_ADDRESS {
                    Value: self.view as *mut c_void,
                });
                self.view = ptr::null();
            }
            if !self.handle.is_null() {
                CloseHandle(self.handle);
                self.handle = null_mut();
            }
        }
    }

    pub fn read_snapshot(&mut self) -> Option<SharedSnapshot> {
        if !self.try_open() {
            return None;
        }
        let shm = unsafe { &*self.view };
        read_seqlock_snapshot(shm)
    }
}

impl Drop for ShmReader {
    fn drop(&mut self) {
        self.close();
    }
}

pub fn read_seqlock_snapshot(shm: &AuraM605HallSharedMemory) -> Option<SharedSnapshot> {
    for _ in 0..8 {
        let s1 = shm.seq.load(Ordering::Acquire);
        if (s1 & 1) != 0 {
            std::hint::spin_loop();
            continue;
        }
        fence(Ordering::Acquire);

        let magic = unsafe { ptr::read_volatile(&shm.magic) };
        let version = unsafe { ptr::read_volatile(&shm.version) };
        let active = unsafe { ptr::read_volatile(&shm.active) };
        let timestamp_us = unsafe { ptr::read_volatile(&shm.timestamp_us) };
        let packet_count = unsafe { ptr::read_volatile(&shm.packet_count) };
        let key_count = unsafe { ptr::read_volatile(&shm.key_count) };
        let vid = unsafe { ptr::read_volatile(&shm.vid) };
        let pid = unsafe { ptr::read_volatile(&shm.pid) };
        let mut keys = [AuraM605HallSharedKey::default(); HALL_KEY_COUNT];
        for (dst, src) in keys.iter_mut().zip(shm.keys.iter()) {
            *dst = unsafe { ptr::read_volatile(src) };
        }

        fence(Ordering::Acquire);
        let s2 = shm.seq.load(Ordering::Acquire);
        if s1 == s2
            && magic == HALL_SHM_MAGIC
            && version == HALL_SHM_VERSION
            && key_count as usize == HALL_KEY_COUNT
        {
            return Some(SharedSnapshot {
                active: active != 0,
                timestamp_us,
                packet_count,
                vid,
                pid,
                keys,
            });
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_layout_matches_cpp() {
        assert_eq!(size_of::<AuraM605HallSharedKey>(), 16);
        assert_eq!(size_of::<AuraM605HallSharedMemory>(), 1128);
    }

    #[test]
    fn seqlock_roundtrip_reads_clean_snapshot() {
        let mut shm = AuraM605HallSharedMemory {
            magic: HALL_SHM_MAGIC,
            version: HALL_SHM_VERSION,
            seq: AtomicU32::new(2),
            active: 1,
            timestamp_us: 987654,
            packet_count: 15,
            key_count: HALL_KEY_COUNT as u32,
            vid: 0x0B05,
            pid: 0x1B7E,
            reserved: 0,
            keys: [AuraM605HallSharedKey::default(); HALL_KEY_COUNT],
        };
        shm.keys[0].hid_usage = 4; // 'A'
        shm.keys[0].logical_id = 0x0602;
        shm.keys[0].normalized = 0.5;
        shm.keys[0].depth_mm = 2.0;
        shm.keys[0].raw = 4336;

        let snap = read_seqlock_snapshot(&shm).expect("snapshot should succeed");
        assert!(snap.active);
        assert_eq!(snap.packet_count, 15);
        assert!((snap.keys[0].normalized - 0.5).abs() < 1e-6);

        // Odd sequence means writer in progress -> returns None
        shm.seq.store(3, Ordering::Release);
        assert!(read_seqlock_snapshot(&shm).is_none());
    }
}
