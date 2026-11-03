#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tauri::State;

mod config;
mod controller;
mod logger;
mod pnp;

use config::{ConfigDocument, Store};
use controller::PnpDeviceController;
use pnp::PnpCatalog;

pub struct AppState {
    pub store: Store,
}

#[derive(serde::Serialize)]
struct DeviceView {
    instance_id: String,
    device_desc: String,
    vid: Option<String>,
    pid: Option<String>,
    serial: Option<String>,
    location_path: Option<String>,
    is_present: bool,
    is_hub: bool,
    problem: u32,
}

#[derive(serde::Serialize)]
struct HubView {
    instance_id: String,
    device_desc: String,
    children: Vec<DeviceView>,
}

#[derive(serde::Serialize)]
struct ScanResponse {
    devices: Vec<DeviceView>,
    hubs: Vec<HubView>,
}

impl From<&pnp::DiscoveredDevice> for DeviceView {
    fn from(d: &pnp::DiscoveredDevice) -> Self {
        Self {
            instance_id: d.instance_id.clone(),
            device_desc: d.device_desc.clone(),
            vid: d.vid.clone(),
            pid: d.pid.clone(),
            serial: d.serial.clone(),
            location_path: d.location_paths.first().cloned(),
            is_present: d.is_present(),
            is_hub: d.is_hub,
            problem: d.problem,
        }
    }
}

#[tauri::command]
fn scan_devices() -> Result<ScanResponse, String> {
    let present = PnpCatalog::scan_present();
    let retained = PnpCatalog::scan_retained();

    let mut merged: Vec<pnp::DiscoveredDevice> = present;
    let present_ids: std::collections::HashSet<String> = merged.iter().map(|d| d.instance_id.clone()).collect();
    for dev in retained {
        if !present_ids.contains(&dev.instance_id) {
            merged.push(dev);
        }
    }

    let tree = pnp::build_hub_tree(&merged);
    let hubs: Vec<HubView> = tree.into_iter().map(|(hub, children)| {
        HubView {
            instance_id: hub.instance_id.clone(),
            device_desc: hub.device_desc.clone(),
            children: children.iter().map(DeviceView::from).collect(),
        }
    }).collect();

    let devices: Vec<DeviceView> = merged.iter().map(DeviceView::from).collect();

    log_info!("scan complete: {} devices, {} hubs", devices.len(), hubs.len());
    Ok(ScanResponse { devices, hubs })
}

#[tauri::command]
fn toggle_device(instance_id: String, enable: bool) -> controller::DeviceActionResult {
    let result = if enable {
        PnpDeviceController::enable(&instance_id)
    } else {
        PnpDeviceController::disable(&instance_id)
    };

    log_info!(
        "toggle {} enable={} success={} error={:?} msg={:?}",
        instance_id, enable, result.success, result.error, result.error_message
    );

    result
}

#[tauri::command]
fn load_config(state: State<'_, AppState>) -> Result<ConfigDocument, String> {
    let (doc, err, corrupt) = state.store.load();
    if corrupt {
        log_error!("config corrupt: {:?}", err);
    }
    if let Some(e) = &err {
        log_error!("config load error: {e}");
    }
    Ok(doc)
}

#[tauri::command]
fn save_config(state: State<'_, AppState>, json: String) -> Result<bool, String> {
    let doc: ConfigDocument = serde_json::from_str(&json).map_err(|e| e.to_string())?;
    state.store.save(&doc)?;
    Ok(true)
}

pub fn run() {
    logger::LOGGER.init();
    log_info!("usbToggle starting");

    let store = Store::new();

    tauri::Builder::default()
        .plugin(tauri_plugin_log::Builder::default().build())
        .manage(AppState { store })
        .invoke_handler(tauri::generate_handler![
            scan_devices,
            toggle_device,
            load_config,
            save_config,
        ])
        .run(tauri::generate_context!())
        .expect("error while running usbToggle application");
}
