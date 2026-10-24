//! The local time zone's offset from UTC, for the times the game shows or names files by (save slot dates:
//! saveslots.rs; session folders: devtools/session.rs).

/// Seconds to add to a UTC Unix time `secs` to get the local wall-clock time at that moment (daylight saving
/// included); 0 where the platform gives no time zone.
pub fn utc_offset_secs(secs: i64) -> i64 {
    imp::utc_offset_secs(secs)
}

/// Windows: FileTimeToLocalFileTime, as the original's save slots (0x004a3020) convert their file times.
#[cfg(target_os = "windows")]
mod imp {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn FileTimeToLocalFileTime(file: *const [u32; 2], local: *mut [u32; 2]) -> i32;
    }

    pub fn utc_offset_secs(secs: i64) -> i64 {
        // FILETIME: 100 ns ticks since 1601
        let ticks = ((secs + 11_644_473_600) as u64) * 10_000_000;
        let ft = [ticks as u32, (ticks >> 32) as u32];
        let mut local = [0u32; 2];
        // SAFETY: both pointers are to live 8-byte FILETIME-shaped arrays
        if unsafe { FileTimeToLocalFileTime(&ft, &mut local) } == 0 {
            return 0;
        }
        let l = (local[0] as u64 | (local[1] as u64) << 32) as i64;
        (l - ticks as i64) / 10_000_000
    }
}

/// macOS, iOS, Linux, Android: localtime_r's `tm_gmtoff`.
#[cfg(unix)]
mod imp {
    pub fn utc_offset_secs(secs: i64) -> i64 {
        let t = secs as libc::time_t;
        // SAFETY: tm is plain data (all-zero is a valid value); localtime_r writes into it and returns it or null
        let mut tm: libc::tm = unsafe { std::mem::zeroed() };
        if unsafe { libc::localtime_r(&t, &mut tm) }.is_null() {
            return 0;
        }
        tm.tm_gmtoff as i64
    }
}

#[cfg(not(any(target_os = "windows", unix)))]
mod imp {
    pub fn utc_offset_secs(_secs: i64) -> i64 {
        0
    }
}

#[cfg(test)]
mod tests {
    /// Every real time zone is within UTC-12 .. UTC+14.
    #[test]
    fn offset_is_a_time_zone() {
        let o = super::utc_offset_secs(1_790_000_000);
        assert!((-12 * 3600..=14 * 3600).contains(&o), "{o}");
    }
}
