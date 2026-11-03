use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;
use uuid::Uuid;

pub const SCHEMA_VERSION: i32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceIdentity {
    pub vid: Option<String>,
    pub pid: Option<String>,
    pub serial: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub location_path: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceRecord {
    pub id: String,
    pub kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_name: Option<String>,
    pub identity: DeviceIdentity,
    #[serde(rename = "lastInstanceIds")]
    pub last_instance_ids: Vec<String>,
    #[serde(rename = "parentHubId", skip_serializing_if = "Option::is_none")]
    pub parent_hub_id: Option<String>,
    #[serde(rename = "lastSeenUtc")]
    pub last_seen_utc: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceGroup {
    pub id: String,
    pub name: String,
    #[serde(rename = "deviceIds")]
    pub device_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSettings {
    pub theme: String,
    pub start_with_windows: bool,
    pub start_in_tray: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConfigDocument {
    #[serde(rename = "schemaVersion")]
    pub schema_version: i32,
    pub settings: AppSettings,
    pub devices: Vec<DeviceRecord>,
    pub groups: Vec<DeviceGroup>,
}

impl Default for ConfigDocument {
    fn default() -> Self {
        Self {
            schema_version: SCHEMA_VERSION,
            settings: AppSettings {
                theme: "System".to_string(),
                start_with_windows: false,
                start_in_tray: false,
            },
            devices: Vec::new(),
            groups: Vec::new(),
        }
    }
}

pub struct Store {
    path: PathBuf,
}

impl Store {
    pub fn new() -> Self {
        let appdata = std::env::var("APPDATA").unwrap_or_else(|_| std::env::temp_dir().to_string_lossy().to_string());
        let path = PathBuf::from(appdata).join("usbToggle").join("config.json");
        Self { path }
    }

    pub fn with_path(path: PathBuf) -> Self {
        Self { path }
    }

    pub fn path(&self) -> &PathBuf {
        &self.path
    }

    pub fn load(&self) -> (ConfigDocument, Option<String>, bool) {
        let mut last_error: Option<String> = None;
        let mut was_corrupt = false;

        if !self.path.exists() {
            let doc = ConfigDocument::default();
            let _ = self.save(&doc);
            return (doc, None, false);
        }

        let json = match fs::read_to_string(&self.path) {
            Ok(j) => j,
            Err(ex) => {
                last_error = Some(ex.to_string());
                return (ConfigDocument::default(), last_error, false);
            }
        };

        match serde_json::from_str::<ConfigDocument>(&json) {
            Ok(mut doc) => {
                if doc.schema_version > SCHEMA_VERSION {
                    last_error = Some(format!("Unsupported schema version {}.", doc.schema_version));
                    return (ConfigDocument::default(), last_error, false);
                }
                migrate(&mut doc);
                (doc, None, false)
            }
            Err(ex) => {
                was_corrupt = true;
                last_error = Some(ex.to_string());
                let _ = backup_corrupt(&self.path, &json);
                (ConfigDocument::default(), last_error, true)
            }
        }
    }

    pub fn save(&self, doc: &ConfigDocument) -> Result<(), String> {
        if doc.schema_version > SCHEMA_VERSION {
            return Err(format!("Refusing to write unsupported schema version {}.", doc.schema_version));
        }
        if let Some(dir) = self.path.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let json = serde_json::to_string_pretty(doc).map_err(|e| e.to_string())?;
        let tmp = self.path.with_extension("json.tmp");
        fs::write(&tmp, &json).map_err(|e| e.to_string())?;
        fs::rename(&tmp, &self.path).map_err(|e| e.to_string())?;
        Ok(())
    }
}

fn migrate(doc: &mut ConfigDocument) {
    if doc.schema_version < 1 {
        doc.schema_version = 1;
    }
    for device in &mut doc.devices {
        if device.id.is_empty() {
            device.id = Uuid::new_v4().to_string();
        }
        if device.last_instance_ids.is_empty() {
            device.last_instance_ids = Vec::new();
        }
    }
    for group in &mut doc.groups {
        if group.id.is_empty() {
            group.id = Uuid::new_v4().to_string();
        }
        if group.device_ids.is_empty() {
            group.device_ids = Vec::new();
        }
    }
}

fn backup_corrupt(path: &PathBuf, json: &str) {
    if let Some(dir) = path.parent() {
        let _ = fs::create_dir_all(dir);
        let ts = chrono::Utc::now().format("%Y%m%dT%H%M%SZ").to_string();
        let backup = dir.join(format!("config.json.{}.bak", ts));
        let _ = fs::write(&backup, json);
    }
}
