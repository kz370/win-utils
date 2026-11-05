mod autostart;
mod classify;
mod config;
mod control;
mod devices;
mod elevation;
mod ico;
mod logger;
mod notify;
mod paths;
mod win;

use serde::Serialize;
use std::collections::HashSet;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use tauri::menu::{CheckMenuItem, Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Emitter, Manager, Theme, WindowEvent};

use config::{Config, Settings, Store};
use classify::Category;
use devices::Device;

const TRAY_ID: &str = "main-tray";
const DEVICE_MENU_PREFIX: &str = "dev:";

/// Set once the page has loaded; after that, hiding is the user's choice.
static UI_READY: AtomicBool = AtomicBool::new(false);

struct Core {
    store: Store,
    config: Mutex<Config>,
    config_error: Mutex<Option<String>>,
    devices: Mutex<Vec<Device>>,
    scan_error: Mutex<Option<String>>,
    scanned_at: Mutex<i64>,
    scanned: AtomicBool,
    busy: Mutex<HashSet<String>>,
    scan_lock: Mutex<()>,
    op_lock: Mutex<()>,
    elevated: bool,
    start_hidden: bool,
}

type Shared = Arc<Core>;

fn core(app: &AppHandle) -> Shared {
    app.state::<Shared>().inner().clone()
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct Snapshot {
    devices: Vec<Device>,
    busy: Vec<String>,
    scanned_at: i64,
    error: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct AppInfo {
    version: String,
    elevated: bool,
    settings: Settings,
    autostart: autostart::AutostartState,
    config_path: String,
    log_dir: String,
    portable: bool,
    config_error: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct ToggleResult {
    ok: bool,
    code: Option<&'static str>,
    message: Option<String>,
    needs_restart: bool,
    snapshot: Snapshot,
}

// ---------------------------------------------------------------- state ---

/// Keyboards and mice are protected automatically when the setting is on.
fn auto_lock(d: &Device, settings: &Settings) -> bool {
    settings.protect_input && matches!(d.category, Category::Keyboard | Category::Mouse | Category::Input)
}

fn apply_config(devices: &mut [Device], cfg: &Config) {
    for d in devices.iter_mut() {
        let custom = cfg.device(&d.id);
        d.custom_name = custom.and_then(|c| c.name.clone()).filter(|n| !n.trim().is_empty());
        d.pinned = custom.map(|c| c.pinned).unwrap_or(false);
        d.custom_icon = custom.and_then(|c| c.icon.clone());
        let auto = auto_lock(d, &cfg.settings);
        let explicit = custom.and_then(|c| c.locked);
        d.locked = config::is_locked(auto, explicit);
        d.auto_locked = auto && explicit != Some(true);
        d.name = d.custom_name.clone().unwrap_or_else(|| d.default_name.clone());
    }
    devices.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()).then_with(|| a.id.cmp(&b.id)));
}

fn snapshot(core: &Core) -> Snapshot {
    let mut busy: Vec<String> = core.busy.lock().unwrap().iter().cloned().collect();
    busy.sort();
    Snapshot {
        devices: core.devices.lock().unwrap().clone(),
        busy,
        scanned_at: *core.scanned_at.lock().unwrap(),
        error: core.scan_error.lock().unwrap().clone(),
    }
}

/// Rescans devices, then updates the tray and the window.
fn refresh(app: &AppHandle) -> Snapshot {
    let core = core(app);
    {
        let _scan = core.scan_lock.lock().unwrap();
        match devices::scan() {
            Ok(mut list) => {
                apply_config(&mut list, &core.config.lock().unwrap());
                *core.devices.lock().unwrap() = list;
                *core.scan_error.lock().unwrap() = None;
            }
            Err(e) => {
                log_error!("scan failed: {e}");
                *core.scan_error.lock().unwrap() = Some(e);
            }
        }
        *core.scanned_at.lock().unwrap() = chrono::Utc::now().timestamp_millis();
        core.scanned.store(true, Ordering::SeqCst);
    }
    publish(app)
}

/// Re-applies names/pins without rescanning hardware.
fn reapply_config(app: &AppHandle) -> Snapshot {
    let core = core(app);
    {
        let cfg = core.config.lock().unwrap().clone();
        apply_config(&mut core.devices.lock().unwrap(), &cfg);
    }
    publish(app)
}

/// Sends the current state to the UI and rebuilds the tray menu.
/// Must be called without holding any of the core locks.
fn publish(app: &AppHandle) -> Snapshot {
    let snap = snapshot(&core(app));
    let _ = app.emit("devices", &snap);
    update_tray(app, &snap);
    snap
}

fn set_busy(app: &AppHandle, id: &str, busy: bool) {
    let core = core(app);
    let list: Vec<String> = {
        let mut set = core.busy.lock().unwrap();
        if busy {
            set.insert(id.to_string());
        } else {
            set.remove(id);
        }
        set.iter().cloned().collect()
    };
    let _ = app.emit("busy", list);
}

fn toggle_device(app: &AppHandle, id: &str, enable: bool) -> ToggleResult {
    let core = core(app);
    let locked = core.devices.lock().unwrap().iter().any(|d| d.id.eq_ignore_ascii_case(id) && d.locked);
    if locked && !enable {
        return ToggleResult {
            ok: false,
            code: Some("locked"),
            message: Some("This device is protected, so it can't be turned off. Unprotect it first.".into()),
            needs_restart: false,
            snapshot: snapshot(&core),
        };
    }
    set_busy(app, id, true);
    let result = {
        let _op = core.op_lock.lock().unwrap();
        if core.elevated {
            control::set_enabled(id, enable)
        } else {
            Err(control::ActionError::new(
                "accessDenied",
                "Turning devices on or off needs administrator rights. Restart USB Toggle as administrator.",
            ))
        }
    };
    set_busy(app, id, false);

    match &result {
        Ok(_) => log_info!("{} {id}: ok", if enable { "enable" } else { "disable" }),
        Err(e) => log_error!("{} {id}: {} ({})", if enable { "enable" } else { "disable" }, e.message, e.code),
    }

    let snap = refresh(app);
    match result {
        Ok(outcome) => {
            let now = snap.devices.iter().find(|d| d.id.eq_ignore_ascii_case(id));
            let stuck = now.map(|d| d.present && d.enabled != enable).unwrap_or(false);
            let needs_restart = outcome.needs_restart || stuck;
            ToggleResult {
                ok: true,
                code: None,
                message: needs_restart.then(|| "Windows will finish this change after a restart.".to_string()),
                needs_restart,
                snapshot: snap,
            }
        }
        Err(e) => ToggleResult { ok: false, code: Some(e.code), message: Some(e.message), needs_restart: false, snapshot: snap },
    }
}

// ----------------------------------------------------------------- tray ---

fn update_tray(app: &AppHandle, snap: &Snapshot) {
    let Some(tray) = app.tray_by_id(TRAY_ID) else { return };
    match build_tray_menu(app, snap) {
        Ok(menu) => {
            let _ = tray.set_menu(Some(menu));
        }
        Err(e) => log_error!("tray menu: {e}"),
    }
    let off = snap.devices.iter().filter(|d| d.present && !d.enabled).count();
    let tip = match off {
        0 => "USB Toggle".to_string(),
        1 => "USB Toggle · 1 device off".to_string(),
        n => format!("USB Toggle · {n} devices off"),
    };
    let _ = tray.set_tooltip(Some(tip));
}

fn build_tray_menu(app: &AppHandle, snap: &Snapshot) -> tauri::Result<Menu<tauri::Wry>> {
    let menu = Menu::new(app)?;
    let pinned: Vec<&Device> = snap.devices.iter().filter(|d| d.pinned).collect();
    if pinned.is_empty() {
        menu.append(&MenuItem::with_id(app, "hint", "Pin devices in the app to switch them from here", false, None::<&str>)?)?;
    } else {
        for d in pinned {
            let busy = snap.busy.iter().any(|b| b == &d.id);
            // A protected device that is on can't be switched from here.
            let frozen = d.locked && d.enabled;
            let mut label = d.name.replace('&', "&&");
            if busy {
                label.push_str("  (working…)");
            } else if frozen {
                label.push_str("  (protected)");
            } else if !d.present {
                label.push_str("  (not connected)");
            }
            let item = CheckMenuItem::with_id(
                app,
                format!("{DEVICE_MENU_PREFIX}{}", d.id),
                label,
                !busy && !frozen,
                d.enabled,
                None::<&str>,
            )?;
            menu.append(&item)?;
        }
    }
    menu.append(&PredefinedMenuItem::separator(app)?)?;
    menu.append(&MenuItem::with_id(app, "show", "Open USB Toggle", true, None::<&str>)?)?;
    menu.append(&MenuItem::with_id(app, "refresh", "Refresh devices", true, None::<&str>)?)?;
    menu.append(&PredefinedMenuItem::separator(app)?)?;
    menu.append(&MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?)?;
    Ok(menu)
}

fn on_menu_event(app: &AppHandle, event: MenuEvent) {
    let id = event.id().as_ref().to_string();
    match id.as_str() {
        "show" => show_main(app),
        "quit" => app.exit(0),
        "refresh" => {
            let app = app.clone();
            std::thread::spawn(move || {
                refresh(&app);
            });
        }
        _ => {
            if let Some(dev_id) = id.strip_prefix(DEVICE_MENU_PREFIX) {
                let app = app.clone();
                let dev_id = dev_id.to_string();
                std::thread::spawn(move || {
                    let enabled = core(&app)
                        .devices
                        .lock()
                        .unwrap()
                        .iter()
                        .find(|d| d.id == dev_id)
                        .map(|d| d.enabled);
                    let Some(enabled) = enabled else { return };
                    let result = toggle_device(&app, &dev_id, !enabled);
                    if !result.ok {
                        show_main(&app);
                        let _ = app.emit("toast", serde_json::json!({ "kind": "error", "message": result.message, "code": result.code }));
                    }
                });
            }
        }
    }
}

/// The tray icon is assets/tray_icon.ico, built into the app. The frame that
/// matches the screen scale is used so it stays crisp instead of being scaled
/// down by the shell.
fn tray_icon(scale: f64) -> Option<tauri::image::Image<'static>> {
    const TRAY_ICO: &[u8] = include_bytes!("../../assets/tray_icon.ico");
    let want = (16.0 * scale).round() as u32;
    let frame = ico::pick_frame(TRAY_ICO, want)?;
    match tauri::image::Image::from_bytes(&frame) {
        Ok(image) => Some(image),
        Err(e) => {
            log_error!("tray_icon.ico could not be read: {e}");
            None
        }
    }
}

fn show_main(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
    }
}

fn toggle_main(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        if w.is_visible().unwrap_or(false) && !w.is_minimized().unwrap_or(false) {
            let _ = w.hide();
        } else {
            show_main(app);
        }
    }
}

fn apply_theme(app: &AppHandle, theme: &str) {
    if let Some(w) = app.get_webview_window("main") {
        let t = match theme {
            "light" => Some(Theme::Light),
            "dark" => Some(Theme::Dark),
            _ => None,
        };
        let _ = w.set_theme(t);
    }
}

// ------------------------------------------------------------- commands ---

async fn blocking<T: Send + 'static>(f: impl FnOnce() -> T + Send + 'static) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(f).await.map_err(|e| e.to_string())
}

#[tauri::command]
async fn get_info(app: AppHandle) -> Result<AppInfo, String> {
    blocking(move || {
        let core = core(&app);
        let settings = core.config.lock().unwrap().settings.clone();
        let config_error = core.config_error.lock().unwrap().clone();
        AppInfo {
            version: app.package_info().version.to_string(),
            elevated: core.elevated,
            settings,
            autostart: autostart::state(),
            config_path: core.store.path().display().to_string(),
            log_dir: logger::log_dir().display().to_string(),
            portable: paths::is_portable(),
            config_error,
        }
    })
    .await
}

#[tauri::command]
async fn get_devices(app: AppHandle, force: bool) -> Result<Snapshot, String> {
    blocking(move || {
        let core = core(&app);
        if force || !core.scanned.load(Ordering::SeqCst) {
            refresh(&app)
        } else {
            snapshot(&core)
        }
    })
    .await
}

#[tauri::command]
async fn set_device_enabled(app: AppHandle, id: String, enabled: bool) -> Result<ToggleResult, String> {
    blocking(move || toggle_device(&app, &id, enabled)).await
}

fn save_config(core: &Core) -> Result<(), String> {
    let cfg = core.config.lock().unwrap().clone();
    core.store.save(&cfg).inspect_err(|e| log_error!("{e}"))
}

#[tauri::command]
async fn rename_device(app: AppHandle, id: String, name: Option<String>) -> Result<Snapshot, String> {
    blocking(move || {
        let core = core(&app);
        let name = name.map(|n| n.trim().to_string()).filter(|n| !n.is_empty());
        core.config.lock().unwrap().update_device(&id, |d| d.name = name);
        save_config(&core)?;
        Ok(reapply_config(&app))
    })
    .await?
}

#[tauri::command]
async fn set_pinned(app: AppHandle, id: String, pinned: bool) -> Result<Snapshot, String> {
    blocking(move || {
        let core = core(&app);
        core.config.lock().unwrap().update_device(&id, |d| d.pinned = pinned);
        save_config(&core)?;
        Ok(reapply_config(&app))
    })
    .await?
}

#[tauri::command]
async fn set_locked(app: AppHandle, id: String, locked: bool) -> Result<Snapshot, String> {
    blocking(move || {
        let core = core(&app);
        let settings = core.config.lock().unwrap().settings.clone();
        let (auto, currently_locked) = core
            .devices
            .lock()
            .unwrap()
            .iter()
            .find(|d| d.id.eq_ignore_ascii_case(&id))
            .map(|d| (auto_lock(d, &settings), d.locked))
            .unwrap_or((false, false));
        if !locked && currently_locked && !config::may_unprotect(settings.protect_input) {
            // The window never offers this; refuse it here too.
            return Err("Protection is on. Turn off \"Protect devices\" in Settings to unprotect devices.".into());
        }
        // Only the user's own protection is stored; the automatic rule needs no entry.
        let choice = (locked && !auto).then_some(true);
        core.config.lock().unwrap().update_device(&id, |d| d.locked = choice);
        save_config(&core)?;
        Ok(reapply_config(&app))
    })
    .await?
}

#[tauri::command]
async fn set_icon(app: AppHandle, id: String, icon: Option<String>) -> Result<Snapshot, String> {
    let icon = icon.filter(|i| !i.is_empty());
    if let Some(i) = &icon {
        if !config::valid_icon(i) {
            return Err("That icon isn't valid. Use an icon from the list or a small PNG image.".into());
        }
    }
    blocking(move || {
        let core = core(&app);
        core.config.lock().unwrap().update_device(&id, |d| d.icon = icon);
        save_config(&core)?;
        Ok(reapply_config(&app))
    })
    .await?
}

#[tauri::command]
async fn save_settings(app: AppHandle, settings: Settings) -> Result<Settings, String> {
    blocking(move || {
        let core = core(&app);
        core.config.lock().unwrap().settings = settings.clone();
        save_config(&core)?;
        apply_theme(&app, &settings.theme);
        // Settings such as "Protect keyboards and mice" change device state.
        reapply_config(&app);
        Ok(settings)
    })
    .await?
}

#[tauri::command]
async fn set_autostart(app: AppHandle, enabled: bool) -> Result<autostart::AutostartState, String> {
    blocking(move || autostart::set(enabled, core(&app).elevated)).await?
}

#[tauri::command]
fn restart_as_admin(app: AppHandle) -> Result<(), String> {
    let visible = app.get_webview_window("main").and_then(|w| w.is_visible().ok()).unwrap_or(true);
    elevation::relaunch_elevated(if visible { "" } else { "--tray" })?;
    app.exit(0);
    Ok(())
}

#[tauri::command]
fn ui_ready(app: AppHandle) {
    let core = core(&app);
    // Only the first load decides; a page reload must not pop the window up.
    if !UI_READY.swap(true, Ordering::SeqCst) && !core.start_hidden {
        show_main(&app);
    }
}

#[tauri::command]
fn open_folder(app: AppHandle, which: String) -> Result<(), String> {
    let core = core(&app);
    let path = match which.as_str() {
        "config" => core.store.path().parent().map(|p| p.to_path_buf()),
        "logs" => Some(logger::log_dir()),
        _ => None,
    }
    .ok_or("Unknown folder")?;
    let _ = std::fs::create_dir_all(&path);
    std::process::Command::new("explorer.exe").arg(&path).spawn().map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn quit(app: AppHandle) {
    app.exit(0);
}

// ------------------------------------------------------------------ run ---

pub fn run() {
    logger::LOGGER.init();
    elevation::wait_for_previous_instance();

    let args: Vec<String> = std::env::args().collect();
    let store = Store::new();
    let (cfg, config_error) = store.load();
    if let Some(e) = &config_error {
        log_error!("{e}");
    }
    let elevated = elevation::is_elevated();
    let autostarted = args.iter().any(|a| a == autostart::AUTOSTART_ARG);
    let start_hidden: bool = args.iter().any(|a| a == "--tray" || a == "--hidden") || (autostarted && cfg.settings.start_minimized);
    let theme = cfg.settings.theme.clone();
    log_info!("starting v{} elevated={elevated} hidden={start_hidden}", env!("CARGO_PKG_VERSION"));

    let shared: Shared = Arc::new(Core {
        store,
        config: Mutex::new(cfg),
        config_error: Mutex::new(config_error),
        devices: Mutex::new(Vec::new()),
        scan_error: Mutex::new(None),
        scanned_at: Mutex::new(0),
        scanned: AtomicBool::new(false),
        busy: Mutex::new(HashSet::new()),
        scan_lock: Mutex::new(()),
        op_lock: Mutex::new(()),
        elevated,
        start_hidden,
    });

    let result = tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| show_main(app)))
        .manage(shared)
        .invoke_handler(tauri::generate_handler![
            get_info,
            get_devices,
            set_device_enabled,
            rename_device,
            set_pinned,
            set_locked,
            set_icon,
            save_settings,
            set_autostart,
            restart_as_admin,
            ui_ready,
            open_folder,
            quit,
        ])
        .setup(move |app| {
            let handle = app.handle().clone();

            // The window is created here (not from the config) so the
            // portable version can keep the WebView2 profile next to the exe.
            if let Some(cfg) = app.config().app.windows.iter().find(|w| w.label == "main") {
                let mut builder = tauri::WebviewWindowBuilder::from_config(&handle, cfg)?;
                if let Some(dir) = paths::webview_dir() {
                    builder = builder.data_directory(dir);
                }
                builder.build()?;
            }

            let mut tray = TrayIconBuilder::with_id(TRAY_ID)
                .tooltip("USB Toggle")
                .show_menu_on_left_click(false)
                .menu(&Menu::new(&handle)?)
                .on_menu_event(on_menu_event)
                .on_tray_icon_event(|tray, event| {
                    if let TrayIconEvent::Click { button: MouseButton::Left, button_state: MouseButtonState::Up, .. } = event {
                        toggle_main(tray.app_handle());
                    }
                });
            let scale = app.primary_monitor().ok().flatten().map(|m| m.scale_factor()).unwrap_or(1.0);
            if let Some(icon) = tray_icon(scale).or_else(|| app.default_window_icon().cloned()) {
                tray = tray.icon(icon);
            }
            tray.build(app)?;

            apply_theme(&handle, &theme);

            if let Some(window) = app.get_webview_window("main") {
                let h = handle.clone();
                window.on_window_event(move |event| {
                    if let WindowEvent::CloseRequested { api, .. } = event {
                        let to_tray = core(&h).config.lock().unwrap().settings.close_to_tray;
                        if to_tray {
                            api.prevent_close();
                            if let Some(w) = h.get_webview_window("main") {
                                let _ = w.hide();
                            }
                        } else {
                            h.exit(0);
                        }
                    }
                });
            }

            // The window starts hidden and the page shows it after its first
            // paint (no white flash). If the page never reports in, show it anyway.
            if !start_hidden {
                let h = handle.clone();
                std::thread::spawn(move || {
                    std::thread::sleep(std::time::Duration::from_secs(4));
                    if let Some(w) = h.get_webview_window("main") {
                        if !w.is_visible().unwrap_or(true) && !UI_READY.load(Ordering::SeqCst) {
                            show_main(&h);
                        }
                    }
                });
            }

            // First scan in the background so the tray is ready even when
            // the window starts hidden.
            let h = handle.clone();
            std::thread::spawn(move || {
                if !core(&h).scanned.load(Ordering::SeqCst) {
                    refresh(&h);
                }
            });

            let h = handle.clone();
            notify::watch(move || {
                refresh(&h);
            });

            Ok(())
        })
        .run(tauri::generate_context!());

    if let Err(e) = result {
        log_error!("fatal: {e}");
    }
}
