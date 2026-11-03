use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

const MAX_SIZE: u64 = 5 * 1024 * 1024;
const MAX_FILES: usize = 5;

pub struct Logger {
    inner: Mutex<Option<LoggerInner>>,
}

struct LoggerInner {
    path: PathBuf,
}

impl Logger {
    pub fn new() -> Self {
        Self { inner: Mutex::new(None) }
    }

    pub fn init(&self) {
        let local_appdata = std::env::var("LOCALAPPDATA").unwrap_or_else(|_| std::env::temp_dir().to_string_lossy().to_string());
        let dir = PathBuf::from(local_appdata).join("usbToggle").join("logs");
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("usbToggle.log");
        let _ = Self::rotate_dir(&dir);
        *self.inner.lock().unwrap() = Some(LoggerInner { path });
    }

    pub fn log(&self, level: &str, message: &str) {
        let path = {
            let guard = self.inner.lock().unwrap();
            let Some(inner) = guard.as_ref() else { return };
            inner.path.clone()
        };

        let ts = chrono::Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();
        let line = format!("[{ts}] [{level}] {message}\n");

        if let Ok(metadata) = fs::metadata(&path) {
            if metadata.len() > MAX_SIZE {
                let _ = Self::rotate_dir(path.parent().unwrap());
            }
        }

        if let Ok(mut file) = fs::OpenOptions::new().create(true).append(true).open(&path) {
            let _ = file.write_all(line.as_bytes());
        }
    }

    fn rotate_dir(dir: &Path) {
        let mut files: Vec<PathBuf> = match fs::read_dir(dir) {
            Ok(rd) => rd.filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.extension().map(|e| e == "log").unwrap_or(false)).collect(),
            Err(_) => Vec::new(),
        };
        files.sort_by_key(|p| p.metadata().and_then(|m| m.modified()).unwrap_or(std::time::SystemTime::UNIX_EPOCH));
        while files.len() > MAX_FILES {
            if let Some(oldest) = files.first() {
                let _ = fs::remove_file(oldest);
                files.remove(0);
            }
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

pub static LOGGER: Logger = Logger { inner: Mutex::new(None) };
