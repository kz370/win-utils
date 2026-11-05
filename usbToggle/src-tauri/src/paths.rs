//! Where the app keeps its files.
//!
//! Installed: settings in %APPDATA%\usbToggle, logs in %LOCALAPPDATA%\usbToggle.
//! Portable (a `portable.txt` file next to the exe): everything in a `data`
//! folder next to the exe, so the folder can be moved or put on a USB stick.

use std::path::PathBuf;
use std::sync::OnceLock;

pub const PORTABLE_MARKER: &str = "portable.txt";

/// The portable data folder, when running as the portable version.
pub fn portable_dir() -> Option<&'static PathBuf> {
    static DIR: OnceLock<Option<PathBuf>> = OnceLock::new();
    DIR.get_or_init(|| {
        let exe = std::env::current_exe().ok()?;
        let dir = exe.parent()?;
        dir.join(PORTABLE_MARKER).is_file().then(|| dir.join("data"))
    })
    .as_ref()
}

pub fn is_portable() -> bool {
    portable_dir().is_some()
}

fn env_dir(var: &str) -> PathBuf {
    std::env::var_os(var).map(PathBuf::from).unwrap_or_else(std::env::temp_dir)
}

pub fn config_dir() -> PathBuf {
    match portable_dir() {
        Some(d) => d.clone(),
        None => env_dir("APPDATA").join("usbToggle"),
    }
}

pub fn log_dir() -> PathBuf {
    match portable_dir() {
        Some(d) => d.join("logs"),
        None => env_dir("LOCALAPPDATA").join("usbToggle").join("logs"),
    }
}

/// WebView2 profile folder; `None` uses Tauri's default under %LOCALAPPDATA%.
pub fn webview_dir() -> Option<PathBuf> {
    portable_dir().map(|d| d.join("webview"))
}
