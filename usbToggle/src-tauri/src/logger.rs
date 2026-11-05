//! Tiny file logger: %LOCALAPPDATA%\usbToggle\logs\usbToggle.log, rotated at 1 MB.

use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;

const MAX_SIZE: u64 = 1024 * 1024;

pub struct Logger {
    path: Mutex<Option<PathBuf>>,
}

pub static LOGGER: Logger = Logger { path: Mutex::new(None) };

pub fn log_dir() -> PathBuf {
    crate::paths::log_dir()
}

impl Logger {
    pub fn init(&self) {
        let dir = log_dir();
        if fs::create_dir_all(&dir).is_ok() {
            if let Ok(mut p) = self.path.lock() {
                *p = Some(dir.join("usbToggle.log"));
            }
        }
    }

    pub fn log(&self, level: &str, message: &str) {
        let Ok(guard) = self.path.lock() else { return };
        let Some(path) = guard.as_ref() else { return };
        if fs::metadata(path).map(|m| m.len() > MAX_SIZE).unwrap_or(false) {
            let _ = fs::rename(path, path.with_extension("old.log"));
        }
        if let Ok(mut f) = fs::OpenOptions::new().create(true).append(true).open(path) {
            let ts = chrono::Local::now().format("%Y-%m-%d %H:%M:%S");
            let _ = writeln!(f, "[{ts}] [{level}] {message}");
        }
    }
}

#[macro_export]
macro_rules! log_info {
    ($($arg:tt)*) => { $crate::logger::LOGGER.log("INFO", &format!($($arg)*)) };
}

#[macro_export]
macro_rules! log_error {
    ($($arg:tt)*) => { $crate::logger::LOGGER.log("ERROR", &format!($($arg)*)) };
}
