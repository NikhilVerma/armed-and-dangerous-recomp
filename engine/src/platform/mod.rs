//! What differs between the operating systems the engine runs on, in one place: where the game keeps its files, which
//! system font it reads, the local time zone, and how the pointer is held in play. Everything else in the engine is
//! portable (Bevy, wgpu: DX12 / Vulkan on Windows, Metal on macOS and iOS; cpal: WASAPI / CoreAudio; Jolt), so a
//! system-specific branch belongs here, behind the narrowest correct cfg: `target_os = "windows"`, `target_vendor =
//! "apple"` for what macOS and iOS share, `target_os = "macos"` for the desktop-only parts (the pointer, windows).
//! docs/macos.md lists what is known to differ and what an iOS build would still need.
//!
//! API: [`app_data_dir`], [`user_files_dir`], [`find_system_font`], [`utc_offset_secs`], [`PLAY_POINTER`] /
//! [`PlayPointer`].

mod localtime;

use std::path::{Path, PathBuf};

pub use localtime::utc_offset_secs;

/// The folder name under the platform's data directory.
const APP_DIR: &str = "AndRemaster";

/// The game's per-user data folder (session recordings): `%LOCALAPPDATA%\AndRemaster` on Windows,
/// `~/Library/Application Support/AndRemaster` on macOS, `$XDG_DATA_HOME/AndRemaster` (`~/.local/share`) on Linux;
/// None when the system names none.
pub fn app_data_dir() -> Option<PathBuf> {
    dirs::data_local_dir().map(|d| d.join(APP_DIR))
}

/// Where options.json and saves/ live: next to the executable on the desktop (engine/target/release for a build from
/// source: the player's files stay with their build, and a playtest build never touches them), else the working
/// directory. An app bundle (macOS .app, iOS) would use [`app_data_dir`] instead: its executable's folder is not the
/// player's to write.
pub fn user_files_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default()
}

/// The folders the system keeps its fonts in, most specific first.
fn system_font_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if cfg!(target_os = "windows") {
        dirs.push(std::env::var_os("WINDIR").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(r"C:\Windows")).join("Fonts"));
    }
    if cfg!(target_vendor = "apple") {
        // Arial ships with macOS under Supplemental (10.15 and later), earlier releases kept it in /Library/Fonts
        if let Some(home) = dirs::home_dir() {
            dirs.push(home.join("Library/Fonts"));
        }
        dirs.extend(["/Library/Fonts", "/System/Library/Fonts/Supplemental", "/System/Library/Fonts"].map(PathBuf::from));
    }
    if cfg!(all(unix, not(target_vendor = "apple"))) {
        if let Some(d) = dirs::font_dir() {
            dirs.push(d);
        }
        dirs.extend(["/usr/local/share/fonts", "/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/TTF"].map(PathBuf::from));
    }
    dirs
}

/// The system's copy of a font file (e.g. "arial.ttf"), its name matched without regard to case (Windows: arial.ttf,
/// macOS: Arial.ttf).
pub fn find_system_font(file: &str) -> Option<PathBuf> {
    system_font_dirs().into_iter().find_map(|dir| find_file_ignoring_case(&dir, file))
}

/// `dir/name`, or the entry of `dir` whose name equals `name` ignoring ASCII case.
pub fn find_file_ignoring_case(dir: &Path, name: &str) -> Option<PathBuf> {
    let exact = dir.join(name);
    if exact.is_file() {
        return Some(exact);
    }
    std::fs::read_dir(dir).ok()?.filter_map(Result::ok).find(|e| e.file_name().to_str().is_some_and(|n| n.eq_ignore_ascii_case(name))).map(|e| e.path())
}

/// How the game holds the pointer in play (cursor.rs `grab`, player.rs `grab_cursor`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum PlayPointer {
    /// Hidden and put back at the window's centre every frame (confined to the window with the `clip_cursor`
    /// setting); the look reads the cursor's offset and the raw motion (mouselook.rs). Windows, Linux.
    Recentre,
    /// Hidden and locked in place by the system while the window has the focus; the look reads the raw motion only.
    /// macOS: winit cannot confine the pointer there (only lock it), and moving it every frame (a warp) re-attaches it
    /// to the mouse and holds back mouse events for a moment after each warp.
    Locked,
}

impl PlayPointer {
    /// Whether the system can keep a visible pointer inside the window (winit's `CursorGrabMode::Confined`: Windows,
    /// X11, Wayland; not macOS).
    pub fn can_confine(self) -> bool {
        self == PlayPointer::Recentre
    }
}

/// This platform's [`PlayPointer`].
pub const PLAY_POINTER: PlayPointer = if cfg!(target_os = "macos") { PlayPointer::Locked } else { PlayPointer::Recentre };

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn data_dir_is_named_for_the_game() {
        if let Some(d) = app_data_dir() {
            assert!(d.ends_with(APP_DIR), "{}", d.display());
        }
    }

    #[test]
    fn file_found_in_any_case() {
        let dir = std::env::temp_dir().join(format!("and_platform_case_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("Arial.ttf"), b"x").unwrap();
        let found = find_file_ignoring_case(&dir, "arial.ttf");
        std::fs::remove_dir_all(&dir).ok();
        assert!(found.is_some_and(|p| p.file_name().is_some_and(|n| n.eq_ignore_ascii_case("arial.ttf"))));
    }
}
