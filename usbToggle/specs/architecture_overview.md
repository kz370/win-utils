# usbToggle — Architecture Overview

## 1. Project type

usbToggle (product name "USB Toggle") is a **Windows desktop tray application** that lists the
USB devices attached to the PC and lets the user enable or disable each one, the same operation
as Device Manager's *Disable device*. It is a single-process desktop app with:

- a native back end written in Rust that talks to Windows device-management APIs, and
- a web front end (HTML/CSS/JavaScript) rendered by Microsoft Edge WebView2 inside a Tauri window.

The app is distributed as an Inno Setup installer and as a portable ZIP.

Target platform: **Windows 10 and 11, x64**. The Rust code compiles on other platforms only as
stubs that report "only supported on Windows"; there is no cross-platform functionality.

## 2. Technologies

| Area | Technology | Notes |
| --- | --- | --- |
| Application framework | Tauri 2 (`tauri` crate 2.x, features `tray-icon`, `image-png`, `image-ico`) | Window, tray icon, IPC, event bus |
| Single instance | `tauri-plugin-single-instance` 2.x | A second launch focuses the running window |
| Back-end language | Rust 2021 edition (minimum 1.77.2) | `src-tauri/src/*.rs` |
| Windows API bindings | `windows-sys` 0.61 | SetupAPI, CfgMgr32, Security, Threading, Shell |
| Serialization | `serde`, `serde_json` | IPC payloads and `config.json` |
| Time | `chrono` | Log timestamps, backup file names, scan timestamps |
| Front end | Plain HTML5, CSS3, ES2020 JavaScript | No framework, no bundler, no build step |
| Web runtime | Microsoft Edge WebView2 | Provided by Windows 10/11 |
| Build tooling | Cargo, `@tauri-apps/cli` 2 (via npm), Windows batch | `build.bat` |
| Installer | Inno Setup 6.3+ | `installer/usbToggle.iss` |
| Packaging (portable) | PowerShell `Compress-Archive` | ZIP with a `portable.txt` marker |

Windows facilities used at run time:

| Facility | Used for |
| --- | --- |
| SetupAPI (`setupapi.dll`) | Enumerating USB device nodes, reading device properties, enabling/disabling devices (`DIF_PROPERTYCHANGE`), writing the persistent disable flag |
| Configuration Manager (`cfgmgr32.dll`) | Device status and problem codes, parent/child tree walking, device-change notifications (`CM_Register_Notification`) |
| Device properties (`DEVPKEY_*`) | Bus-reported product name, arrival/removal dates, physical port location (ACPI `_PLD`) |
| Security / tokens (`advapi32.dll`) | Detecting whether the process is elevated |
| Shell (`shell32.dll`) | Relaunching the app elevated (`ShellExecuteW` with the `runas` verb) |
| Task Scheduler (`schtasks.exe`) and registry (`reg.exe`) | "Start with Windows" |
| Explorer (`explorer.exe`) | Opening the settings and log folders |

## 3. Architecture pattern

The app follows a **layered, single-process architecture with a central state holder**:

```
Presentation    ui/ (index.html, app.js, styles.css, icons.js)        WebView2
                       |  Tauri IPC: commands (request/response) + events (push)
Application     src-tauri/src/lib.rs — Core state, commands, tray, window lifecycle
                       |
Domain          devices.rs (discovery + grouping)   classify.rs (naming, categories)
                control.rs (enable/disable)          config.rs (settings, persistence)
                autostart.rs  elevation.rs  notify.rs
                       |
Platform        win.rs, paths.rs, logger.rs, windows-sys bindings → Windows APIs
```

Key design decisions:

1. **The back end owns all state.** `lib.rs` keeps the latest device list (`Core.devices`),
   the configuration, and the set of devices with an operation in progress. The window and the
   tray menu are both views of that state. Every change (scan, toggle, rename, pin) ends with
   `publish()`, which pushes a `devices` event to the window and rebuilds the tray menu.
2. **Live state is never cached on disk.** Whether a device is on or off is always read from
   Windows. `config.json` stores only user preferences: settings, custom names, pins and lock (protection) choices.
3. **The device instance ID is the identity.** Windows stores the enabled/disabled state per
   device instance, so the app uses the instance ID as the key for names, pins and locks.
4. **Pure logic is separated from Win32 calls.** `classify.rs` and `devices::assemble()` are
   plain data transformations with unit tests; the Win32 code in `devices::imp` and
   `control::imp` only gathers raw data or performs the action.
5. **Blocking work never runs on the UI thread.** All IPC commands that touch Windows are
   `async` and run their work through `spawn_blocking`.
6. **Serialization of side effects.** Device scans are serialized by `scan_lock` and
   enable/disable operations by `op_lock`. Locks are never held while calling Tauri APIs that
   dispatch to the main thread, to avoid deadlocks with menu and window event handlers.

## 4. Module boundaries

| Module | Layer | Responsibility | Public surface |
| --- | --- | --- | --- |
| `main.rs` | Entry | Binary entry point; hides the console in release builds | `main()` |
| `lib.rs` | Application | Core state, IPC commands, tray menu, window lifecycle, startup | `run()` |
| `devices.rs` | Domain | Find USB device nodes, fold interfaces into devices, link hubs, decide built-in and panel | `Device`, `scan()` |
| `classify.rs` | Domain (pure) | Categories, display names, VID/PID, port number, `_PLD` panel parsing, vendor table | `NodeInfo`, `Category`, helper functions |
| `control.rs` | Domain | Enable or disable one device instance | `set_enabled()`, `ActionError`, `ActionOutcome` |
| `config.rs` | Domain | Settings and per-device customizations; load, migrate, back up, save | `Config`, `Settings`, `DeviceSettings`, `Store` |
| `autostart.rs` | Domain | "Start with Windows" via logon task or Run key | `state()`, `set()`, `AutostartState` |
| `elevation.rs` | Platform | Elevation check, elevated relaunch, wait for the previous instance | `is_elevated()`, `relaunch_elevated()`, `wait_for_previous_instance()` |
| `notify.rs` | Platform | Device-change notifications with debouncing, polling fallback | `watch()` |
| `ico.rs` | Platform | Picks the frame of a multi-size `.ico` that fits the screen scale (tray icon) | `pick_frame()` |
| `paths.rs` | Platform | Installed vs. portable data locations | `config_dir()`, `log_dir()`, `webview_dir()`, `is_portable()` |
| `logger.rs` | Platform | Small rotating file logger | `LOGGER`, `log_info!`, `log_error!`, `log_dir()` |
| `win.rs` | Platform | UTF-16 helpers, last error, console-less child processes | `wide()`, `wide_to_string()`, `last_error()`, `hidden_command()` |
| `ui/app.js` | Presentation | State, rendering, interactions, IPC calls | none (self-contained IIFE) |
| `ui/icons.js` | Presentation | Inline SVG icon set | `window.ICONS`, `window.icon()` |

## 5. Infrastructure components

| Component | Location | Role |
| --- | --- | --- |
| Tauri configuration | `src-tauri/tauri.conf.json` | Product name, version, identifier, window definition (created in code), bundle icons; Tauri bundling disabled |
| Capabilities | `src-tauri/capabilities/default.json` | Grants `core:default` permissions to the `main` window |
| Cargo manifest | `src-tauri/Cargo.toml` | Dependencies, `usbToggle` binary name, size-optimized release profile |
| Build script | `build.bat` | Builds the exe, the portable ZIP and the Inno installer into `dist\` |
| Installer | `installer/usbToggle.iss` | Per-machine install, WebView2 check, clean uninstall |
| Portable marker | `installer/portable.txt` | Switches the app to portable data storage |
| Icons | `assets/` | `app_icon.ico` (window, taskbar, exe, installer), `tray_icon.ico`, `header_logo.png`, editable SVG sources in `assets/source/` |

Files written at run time:

| File | Installed mode | Portable mode |
| --- | --- | --- |
| Settings | `%APPDATA%\usbToggle\config.json` | `<exe dir>\data\config.json` |
| Log | `%LOCALAPPDATA%\usbToggle\logs\usbToggle.log` (+ `.old.log`) | `<exe dir>\data\logs\` |
| WebView2 profile | `%LOCALAPPDATA%\com.usbtoggle.app\` | `<exe dir>\data\webview\` |
| Startup entry | Scheduled task `usbToggle` or `HKCU\...\Run\usbToggle` | Same |

## 6. Service relationships

- **Window ↔ Core.** The window calls IPC commands (`get_devices`, `set_device_enabled`, ...)
  and listens to events (`devices`, `busy`, `toast`). See `api_reference.md`.
- **Tray ↔ Core.** The tray menu is rebuilt from the same snapshot after every change. Clicking
  a pinned device in the tray runs the same `toggle_device()` as the window.
- **Core → Windows.** Scans go through `devices::scan()`; switching goes through
  `control::set_enabled()`; both use SetupAPI/CfgMgr32.
- **Windows → Core.** `notify::watch()` registers for device-instance notifications. Bursts are
  debounced (350 ms of quiet) and then trigger `refresh()`.
- **Core ↔ disk.** `config::Store` loads once at startup and saves after each settings, name
  or pin change. `logger` appends to the log file.
- **Process ↔ process.** The single-instance plugin forwards a second launch to the first
  instance. "Restart as admin" starts an elevated copy with `--wait-for-pid`, and the new copy
  waits for the old one to exit before starting.

## 7. High-level behavior

1. **Startup.** Initialize the logger; wait for a previous instance if relaunched elevated;
   load the configuration; detect elevation; decide whether to start hidden (`--tray`,
   `--hidden`, or `--autostart` with *Start in the tray* enabled).
2. **Setup.** Create the main window (hidden, with a portable WebView2 folder if needed), create
   the tray icon at the right size for the monitor's DPI, apply the theme, install the
   close-to-tray handler, start the first scan in the background, and start watching for device
   changes.
3. **First paint.** The page renders a skeleton, calls `ui_ready` after its first frame, and the
   back end shows the window. If the page never reports in, the window is shown after 4 seconds.
4. **Scanning.** Each refresh enumerates all USB device nodes (connected and remembered),
   reduces them to physical devices, applies custom names, pins and protection, sorts by name, stores the
   result, emits `devices`, and rebuilds the tray menu.
5. **Switching.** A toggle marks the device busy, checks elevation, calls the class installer
   (or flips the stored disable flag for a disconnected device), rescans, verifies the new state,
   and returns a result that the window turns into a toast with *Undo*.
6. **Lifecycle.** Closing the window hides it when *Keep running in the tray* is on. *Quit* in
   the tray menu or in Settings exits the process.

## 8. Diagrams

- `diagrams/architecture.mmd` — component view
- `diagrams/dataflow.mmd` — how device data and user actions flow
- `diagrams/classes.mmd` — data structures and their relationships
- `diagrams/sequence.mmd` — enabling/disabling a device from the window
