//! Settings and per-device customisations, stored as JSON in
//! %APPDATA%\usbToggle\config.json. The on/off state is never stored: it is
//! always read live from Windows.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;

pub const SCHEMA_VERSION: u32 = 2;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Settings {
    /// "system" | "light" | "dark"
    pub theme: String,
    /// Closing the window keeps the app running in the tray.
    pub close_to_tray: bool,
    /// When started with Windows, stay in the tray instead of opening.
    pub start_minimized: bool,
    pub show_disconnected: bool,
    pub show_hubs: bool,
    /// Ask before turning off keyboards, mice, hubs and Bluetooth adapters.
    pub confirm_risky: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            theme: "system".into(),
            close_to_tray: true,
            start_minimized: true,
            show_disconnected: true,
            show_hubs: false,
            confirm_risky: true,
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct DeviceSettings {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub pinned: bool,
}

impl DeviceSettings {
    pub fn is_empty(&self) -> bool {
        self.name.is_none() && !self.pinned
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Config {
    pub schema_version: u32,
    pub settings: Settings,
    /// Keyed by upper-case device instance ID.
    pub devices: BTreeMap<String, DeviceSettings>,
}

impl Default for Config {
    fn default() -> Self {
        Self { schema_version: SCHEMA_VERSION, settings: Settings::default(), devices: BTreeMap::new() }
    }
}

impl Config {
    pub fn device(&self, id: &str) -> Option<&DeviceSettings> {
        self.devices.get(&id.to_ascii_uppercase())
    }

    pub fn update_device(&mut self, id: &str, f: impl FnOnce(&mut DeviceSettings)) {
        let key = id.to_ascii_uppercase();
        let entry = self.devices.entry(key.clone()).or_default();
        f(entry);
        if entry.is_empty() {
            self.devices.remove(&key);
        }
    }
}

pub struct Store {
    path: PathBuf,
}

impl Store {
    pub fn new() -> Self {
        let base = std::env::var_os("APPDATA").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
        Self { path: base.join("usbToggle").join("config.json") }
    }

    pub fn path(&self) -> &PathBuf {
        &self.path
    }

    /// Loads the config. A file that can't be read is backed up and replaced
    /// by defaults; the returned message explains what happened.
    pub fn load(&self) -> (Config, Option<String>) {
        let text = match fs::read_to_string(&self.path) {
            Ok(t) => t,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return (Config::default(), None),
            Err(e) => return (Config::default(), Some(format!("Couldn't read settings: {e}"))),
        };
        match serde_json::from_str::<Config>(&text) {
            Ok(mut cfg) if cfg.schema_version <= SCHEMA_VERSION => {
                cfg.schema_version = SCHEMA_VERSION;
                (cfg, None)
            }
            Ok(cfg) => (
                Config::default(),
                Some(format!(
                    "Settings were saved by a newer version (schema {}); using defaults for now.",
                    cfg.schema_version
                )),
            ),
            Err(e) => {
                let backup = self.backup(&text);
                (
                    Config::default(),
                    Some(format!(
                        "Settings file was damaged ({e}) and has been reset.{}",
                        backup.map(|p| format!(" A copy was saved to {}.", p.display())).unwrap_or_default()
                    )),
                )
            }
        }
    }

    fn backup(&self, text: &str) -> Option<PathBuf> {
        let ts = chrono::Local::now().format("%Y%m%d-%H%M%S");
        let path = self.path.with_file_name(format!("config.{ts}.bak.json"));
        fs::write(&path, text).ok().map(|_| path)
    }

    pub fn save(&self, cfg: &Config) -> Result<(), String> {
        if let Some(dir) = self.path.parent() {
            fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        }
        let json = serde_json::to_string_pretty(cfg).map_err(|e| e.to_string())?;
        let tmp = self.path.with_extension("json.tmp");
        fs::write(&tmp, json).map_err(|e| format!("Couldn't save settings: {e}"))?;
        // rename() replaces the destination atomically on Windows (MoveFileEx).
        fs::rename(&tmp, &self.path).map_err(|e| format!("Couldn't save settings: {e}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn old_or_partial_files_load() {
        let cfg: Config = serde_json::from_str(r#"{"schemaVersion":1,"settings":{"theme":"dark"}}"#).unwrap();
        assert_eq!(cfg.settings.theme, "dark");
        assert!(cfg.settings.close_to_tray);
        assert!(cfg.devices.is_empty());
    }

    #[test]
    fn empty_device_entries_are_dropped() {
        let mut cfg = Config::default();
        cfg.update_device(r"usb\vid_1&pid_2\3", |d| d.pinned = true);
        assert!(cfg.device(r"USB\VID_1&PID_2\3").unwrap().pinned);
        cfg.update_device(r"USB\VID_1&PID_2\3", |d| d.pinned = false);
        assert!(cfg.devices.is_empty());
    }
}
