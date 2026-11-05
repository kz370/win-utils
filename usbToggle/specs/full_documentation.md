# usbToggle — Full Documentation

This document describes the usbToggle application in the `usbToggle/` folder of the
`win-utils` repository: what it does, how it is built, every file it contains, the rules it
follows, its data models and its main flows. Paths are relative to `usbToggle/`.

Companion documents in `specs/`:

| Document | Contents |
| --- | --- |
| `architecture_overview.md` | Technologies, architecture pattern, module boundaries |
| `developer_guide.md` | Setup, building, running, debugging, extending |
| `api_reference.md` | The IPC commands and events between the window and the back end |
| `catalog.txt` | One-line index of every module, type, function and UI component |
| `relationships.txt` | Who uses whom |
| `data_models.txt` | Every data structure, schema and serialized format |
| `diagrams/*.mmd` | Mermaid diagrams |
| `runtime.md` | Processing log of this documentation run |

---

## 1. Introduction

USB Toggle is a Windows tray application for turning USB devices on and off. Turning a device
off has the same effect as *Disable device* in Device Manager: Windows unloads the device's
driver and the device stops working. The setting persists across unplugging and restarts until
the device is turned back on. The USB port itself may stay powered.

Main capabilities:

- Lists the physical USB devices attached to the PC, with readable names, a device type,
  the port number, the front/back panel location and any Windows problem code.
- Switches each device on or off with one click, with progress feedback and *Undo*.
- Hides devices built into the PC (laptop webcam, Bluetooth adapter, fingerprint reader,
  motherboard hub chips) by default, so only devices on real USB ports are listed.
- Hides devices that are not working (Windows problem codes such as 43) by default.
- Groups connected devices by the panel of the case they are plugged into (front, back, ...).
- Optionally lists USB hubs and devices Windows remembers that are not plugged in.
- Lets the user rename and pin devices (pinned devices also appear in the tray menu) and choose an icon
  for each device from 22 built-in icons or a searchable library of 239 Lucide icons.
- Settings are edited in a dialog and applied only with *Save*; leaving with unsaved changes asks
  whether to save or discard them.
- All application icons (window, taskbar, tray, installer, header) can be replaced in one step with
  `set-icons.bat`.
- Updates the list automatically when devices are plugged in or removed.
- Protects devices from being turned off: keyboards and mice automatically (setting), any other
  device with its lock button. Protected devices are grouped in a "Protected" section.
- Asks for confirmation before turning off keyboards, mice, hubs and Bluetooth adapters.
- Runs in the tray, allows a single instance, can restart itself as administrator, and can
  start with Windows (elevated, without a UAC prompt, when set up from an elevated session).
- Ships as an installer and as a portable version.

Windows only allows administrators to enable or disable devices. Without administrator rights
the app still lists everything and offers a *Restart as admin* button.

---

## 2. Architecture

The app is a Tauri 2 application: a Rust back end hosts a WebView2 window that renders a plain
HTML/CSS/JavaScript user interface. The two halves communicate through Tauri IPC commands
(request/response) and events (pushed from the back end).

```
ui/ (WebView2)  <-- commands / events -->  lib.rs (Core)  -->  devices / control / config /
                                                            autostart / elevation / notify
                                                                     |
                                                       SetupAPI, CfgMgr32, Task Scheduler,
                                                       registry, shell (Windows)
```

The back end owns all state: the current device list, the configuration and the set of
devices that have an operation in progress. The window and the tray menu only display it.
See `architecture_overview.md` for the full description and `diagrams/architecture.mmd`.

---

## 3. Modules

| Module | File | Summary |
| --- | --- | --- |
| Entry point | `src-tauri/src/main.rs` | Calls `usb_toggle_lib::run()` |
| Application core | `src-tauri/src/lib.rs` | State, IPC commands, tray, window lifecycle, startup |
| Device discovery | `src-tauri/src/devices.rs` | Enumerates USB device nodes and builds `Device` records |
| Classification | `src-tauri/src/classify.rs` | Device types, display names, VID/PID, port, panel, vendors |
| Device control | `src-tauri/src/control.rs` | Enables and disables a device instance |
| Configuration | `src-tauri/src/config.rs` | Settings and per-device names/pins/locks, persistence and migration |
| Autostart | `src-tauri/src/autostart.rs` | "Start with Windows" |
| Elevation | `src-tauri/src/elevation.rs` | Admin detection and elevated relaunch |
| Notifications | `src-tauri/src/notify.rs` | Device-change notifications with debouncing |
| ICO frames | `src-tauri/src/ico.rs` | Picks the right frame of a multi-size `.ico` (tray icon) |
| Paths | `src-tauri/src/paths.rs` | Installed vs. portable data folders |
| Logging | `src-tauri/src/logger.rs` | Rotating log file and logging macros |
| Win32 helpers | `src-tauri/src/win.rs` | UTF-16 conversion, last error, hidden child processes |
| User interface | `ui/index.html`, `ui/app.js`, `ui/styles.css`, `ui/icons.js`, `ui/icon-library.js` (+ generated `ui/logo.png`) | The window |
| Build | `build.bat`, `package.json` | Builds the exe, the portable ZIP and the installer |
| Tools | `set-icons.bat`, `tools/*` | Regenerates the app icons; generates the icon library |
| Installer | `installer/usbToggle.iss`, `installer/portable.txt` | Inno Setup script and portable marker |
| Assets | `assets/**` | Application icon, tray icon, header logo and their SVG sources |

---

## 4. Business rules

The rules below are enforced by the code. Each rule names the place that implements it.

### 4.1 Which devices are listed

| # | Rule | Implemented in |
| --- | --- | --- |
| D1 | Only device nodes enumerated by the USB bus driver (enumerator `USB`) are considered. Nodes from other enumerators (`HID`, `USBSTOR`, ...) are treated as functions of a USB device, not as devices. | `devices::imp::open_set` |
| D2 | A device node is a top-level device when its instance ID starts with `USB\`, its device part does not contain `&MI_` (an interface of a composite device), and it is not a root hub (`ROOT_HUB...`). | `devices::is_top_level` |
| D3 | Connected and remembered (disconnected) devices are both read. A device is *present* when it appears in the `DIGCF_PRESENT` enumeration. | `devices::imp::scan` |
| D4 | For connected devices, every node below the device (up to depth 4, at most 64 nodes) is read and used to classify and name it. The walk does not descend below a Bluetooth radio (class `Bluetooth`), because the nodes there are paired devices, not functions of the USB device. | `devices::imp::descendants` |
| D5 | Interface nodes of a disconnected composite device are attached to their parent only when exactly one top-level device has the same VID:PID; otherwise they are ignored. | `devices::assemble` |
| D6 | A node is a hub when its service is `USBHUB` or `USBHUB3`, or when its hardware/compatible IDs advertise USB class `09`. | `classify::is_hub` |
| D7 | A device is *built-in* when its container ID equals the machine container `{00000000-0000-0000-ffff-ffffffffffff}`. When no container ID is available, the device is built-in when its capabilities do not include `CM_DEVCAP_REMOVABLE`. When neither is known, it is **not** built-in (show rather than hide). | `devices::is_built_in` |
| D8 | A device's parent hub (`hubId`, `hubName`) is reported only when the parent is a hub, is not a root hub and is not built-in. | `devices::assemble` |
| D9 | The panel (front, back, left, right, top, bottom) comes from the `_PLD` buffer in `DEVPKEY_Device_PhysicalDeviceLocation`. A device without its own value uses the nearest ancestor's value (up to 8 levels), stopping at a root hub. Panel value 6 (unknown) and higher, or revision 0, mean "no panel". | `classify::parse_pld_panel`, `devices::panel_of` |
| D10 | A device is disabled when, for a connected device, its problem code is `CM_PROB_DISABLED` (22) or its config flags include `CONFIGFLAG_DISABLED`; for a disconnected device, when its config flags include `CONFIGFLAG_DISABLED`. | `devices::imp::scan` |
| D11 | Problem codes are only reported for connected devices, and only when the devnode status has `DN_HAS_PROBLEM`. | `devices::imp::devnode_status` |
| D12 | The device list is sorted by display name (case-insensitive), then by instance ID. | `lib::apply_config` |

### 4.2 Device types

| # | Rule | Implemented in |
| --- | --- | --- |
| C1 | Hubs are always category `hub`. | `devices::assemble` |
| C2 | Other devices are classified from the device's own class and IDs plus all its functions, checked in this order: Bluetooth, camera, storage, phone, audio, network, printer, security key, game controller, keyboard/mouse, serial, generic HID input, other. The first match wins. Bluetooth comes first so a Bluetooth radio (including the Bluetooth half of a Wi-Fi + Bluetooth combo card, which Windows sees as a USB device) is never classified by its other interfaces. | `classify::categorize` |
| C3 | USB class codes are matched on `CLASS_xx` and `DEVCLASS_xx` only; `SUBCLASS_xx` never counts as a class match. | `classify::has_usb_class` |
| C4 | A device with both keyboard and mouse functions is a mouse if its name mentions a mouse (and not a keyboard), a keyboard if the opposite, otherwise "Keyboard & mouse" (`input`). A keyboard-only device whose name mentions a mouse is a mouse (gaming mice often expose a keyboard interface). | `classify::categorize` |

### 4.3 Device names

| # | Rule | Implemented in |
| --- | --- | --- |
| N1 | The automatic name is chosen from, in order: the friendly name of the device or a function of the matching class (disk drive for storage, camera/image for cameras, WPD for phones, Bluetooth for Bluetooth adapters, so a combo card's Bluetooth half is not named after its Wi-Fi product string); the bus-reported product string; the device's friendly name or description; the first non-HID function name; finally "*Vendor* *Category*" or the category label. | `classify::display_name` |
| N2 | Generic Windows names ("USB Composite Device", "USB Input Device", "Generic USB Hub", "Microsoft Bluetooth Enumerator", ...) are never used as a name. | `classify::is_generic` |
| N3 | A trailing " USB Device" is removed and whitespace is collapsed. | `classify::clean` |
| N4 | The vendor name (from a built-in VID table) is prefixed when the name does not already contain it and the name is short (two words or fewer) or starts with "USB", "Wireless", "Gaming", "Optical" or "Generic". | `classify::display_name` |
| N5 | A custom name, when set and not blank, replaces the automatic name everywhere (window and tray). | `lib::apply_config` |

### 4.4 Switching devices on and off

| # | Rule | Implemented in |
| --- | --- | --- |
| S0 | A protected (locked) device cannot be turned off. The back end refuses with code `locked` before anything else; turning a protected device **on** is allowed. In the window its switch is greyed out while it is on; in the tray its item is disabled and labelled "(protected)". | `lib::toggle_device`, `ui/app.js: setEnabled, updateRow`, `lib::build_tray_menu` |
| S0b | A device is protected automatically when *Protect keyboards and mice* is on (default) and its category is keyboard, mouse or keyboard-and-mouse. The lock button stores a per-device choice only when it differs from the automatic rule; that choice overrides the rule either way. | `lib::auto_lock`, `lib::apply_config`, `lib::set_locked` |
| S1 | Only an elevated process may switch devices. Without elevation the request fails immediately with code `accessDenied` and Windows is not called. | `lib::toggle_device` |
| S2 | Only one switch operation runs at a time (`op_lock`). Other requests wait. | `lib::toggle_device` |
| S3 | A device with an operation in progress is reported as busy (`busy` event); the window disables its switch and the tray disables its menu item. | `lib::set_busy`, `ui/app.js`, `lib::build_tray_menu` |
| S4 | A connected device is switched through the class installer with `DIF_PROPERTYCHANGE`, scope `DICS_FLAG_GLOBAL`. Enabling also attempts the config-specific scope (ignoring errors) to clear a per-profile disable. | `control::imp::change_live` |
| S5 | A disconnected device is switched by setting or clearing `CONFIGFLAG_DISABLED` in its stored config flags. Windows applies it the next time the device is plugged in. | `control::imp::change_offline` |
| S6 | After every operation the device list is rescanned. If Windows reports success but a connected device is still not in the requested state, or the installer asks for a restart, the result says a restart is needed. | `lib::toggle_device`, `control::imp::change_live` |
| S7 | Windows errors are mapped to `accessDenied`, `notDisableable`, `missing` or `failed`, each with a readable message. | `control::imp::map_error` |
| S8 | When enabled in settings (`confirmRisky`, default on), turning **off** a connected keyboard, mouse, keyboard-and-mouse device, hub or Bluetooth adapter from the window asks for confirmation first. "Don't ask again" turns the setting off. Turning devices on never asks. | `ui/app.js: setEnabled, confirmDisable` |
| S9 | Switching from the tray menu never asks for confirmation. If it fails, the window is shown and an error toast is displayed. | `lib::on_menu_event` |
| S10 | A successful switch from the window shows a toast with *Undo*, which performs the opposite operation without confirmation and without a further *Undo*. | `ui/app.js: setEnabled` |

### 4.5 Display rules in the window

| # | Rule | Implemented in |
| --- | --- | --- |
| V1 | Pinned devices are always shown (subject to search and the All/On/Off filter) in a "Pinned" section at the top. | `ui/app.js: render` |
| V2 | Hubs are hidden unless *Show USB hubs* is on (default off) or a search is active. | `ui/app.js: render` |
| V3 | Disconnected devices that are **on** are hidden unless *Show disconnected devices* is on (default off) or a search is active. Disconnected devices that are **off** are always listed, so the user can find them. | `ui/app.js: render` |
| V4 | Built-in devices are hidden when *Only devices on USB ports* is on (default on). A note at the end of the list names up to three of them and offers *Show them*. | `ui/app.js: render, getHiddenNote` |
| V4b | Connected devices with a Windows problem code other than 22 (for example 43 or 28) are hidden when *Hide devices that aren't working* is on (default on). A second note names them and offers *Show them*. Disabled devices are never treated as problems. | `ui/app.js: hasProblem, render, getHiddenNote` |
| V5 | Connected, non-hub, unprotected devices are grouped into sections by panel in the order Front panel, Back panel, Left side, Right side, Top, Bottom. Devices without a panel go into "Other ports" (or "Connected" when no device has a panel). Built-in devices, when shown, get a "Built into this PC" section. Protected devices get a "Protected" section. Then come "USB hubs" and "Not connected" (both collapsible). | `ui/app.js: render` |
| V6 | Inside a panel section, a row does not repeat the panel name. | `ui/app.js: metaParts` |
| V7 | Disconnected devices are sorted with the ones that are off first, then by last-seen time, newest first. | `ui/app.js: render` |
| V8 | The header summary counts connected devices that would be listed (respecting the hub, built-in and problem settings; a pinned device is counted even when it is built-in or has a problem) and how many of them are off. | `ui/app.js: renderSummary` |
| V9 | Search matches every term (space-separated, case-insensitive) against name, default name, description, vendor, type, VID:PID, instance ID, hub name, panel and function names. | `ui/app.js: matches` |

### 4.6 Settings, persistence and startup

| # | Rule | Implemented in |
| --- | --- | --- |
| P1 | Only preferences are stored: settings, custom names, pins, lock choices and icons. On/off state is always read from Windows. | `config.rs` |
| P2 | Per-device entries are keyed by the upper-case instance ID. Entries with no name, no pin, no lock choice and no icon are removed. | `config::Config::update_device` |
| P3 | Saving writes `config.json.tmp` and renames it over `config.json`. | `config::Store::save` |
| P4 | A missing file means defaults with no message. An unreadable file means defaults with a message. A damaged file is copied to `config.<yyyyMMdd-HHmmss>.bak.json` and replaced by defaults, with a message. A file from a newer schema is not overwritten at load time; defaults are used with a message. | `config::Store::load` |
| P5 | Files with schema version below 3 have *Only devices on USB ports* set to on and *Show USB hubs* and *Show disconnected devices* set to off, once; everything else is kept. | `config::Store::load` |
| P6 | Missing fields take their default values (`#[serde(default)]`). | `config.rs` |
| P7 | The app is portable when a file named `portable.txt` exists next to the executable. Then settings, logs and the WebView2 profile live in a `data` folder next to it. | `paths.rs` |
| P8 | The app starts hidden in the tray when launched with `--tray` or `--hidden`, or with `--autostart` while *Start in the tray* is on. | `lib::run` |
| P9 | "Start with Windows" from an elevated session creates the logon task `usbToggle` (run level `HighestAvailable`, 3-second delay, no battery restrictions, no time limit) and removes the Run value. From a non-elevated session it writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\usbToggle`. Turning it off removes both. Both pass `--autostart`. | `autostart.rs` |
| P10 | "Start with Windows" is reported as enabled when the task or the Run value exists, and as elevated when the task exists. | `autostart::imp::state` |
| P11 | Closing the window hides it when *Keep running in the tray* is on (default); otherwise it exits the app. | `lib::run` (window event handler) |
| P12 | A second launch of the app shows the existing window instead of starting another instance. | `tauri-plugin-single-instance` in `lib::run` |
| P13 | *Restart as admin* starts the same executable with the `runas` verb and `--wait-for-pid <current pid>` (plus `--tray` if the window was hidden), then exits. The new process waits up to 5 seconds for the old one to exit. | `elevation.rs`, `lib::restart_as_admin` |
| P14 | The window is shown only after the page reports its first paint (`ui_ready`), and only on the first report. If the page has not reported within 4 seconds of setup, the window is shown anyway (unless starting hidden). | `lib::ui_ready`, `lib::run` |
| P16 | Changes made in the Settings dialog are kept in a draft and applied only by *Save* (or Ctrl+S); this includes *Start with Windows*. The theme is previewed immediately. Closing the dialog (close button, *Cancel*, Esc, click outside) with unsaved changes opens a "Save your changes?" dialog with *Discard* and *Save*; Esc or a click outside that dialog returns to Settings. *Discard* restores the saved theme. Actions outside the dialog ("Show them" notes, "Don't ask again") still save immediately. | `ui/app.js: openSettings, settingsDirty, saveDraft, discardDraft, requestCloseSettings` |
| P15 | The log file is renamed to `usbToggle.old.log` when it grows past 1 MB (the previous `.old.log` is replaced). | `logger.rs` |

### 4.8 Icons

| # | Rule | Implemented in |
| --- | --- | --- |
| I1 | The application icons are three files in `assets/`: `app_icon.ico` (window, taskbar, Start menu, exe, installer), `tray_icon.ico` and `header_logo.png`. They can be replaced by hand (keeping the names) or generated by `set-icons.bat` from a PNG or SVG: one image is used for all three, an optional second image for the tray (the 16/20/24 px frames of the app icon come from the same image). `set-icons.bat` changes nothing without an image or `--reset`; `--reset` rebuilds the files from the built-in artwork in `assets/source/`. | `tools/set-icons.ps1`, `assets/*` |
| I2 | Each device may have a custom icon (`customIcon`). When it is not set or not a known icon name, the icon of the detected category is used ("Auto"). Icon names must match `[a-z0-9-]{1,32}`; others are rejected by the back end. | `lib::set_icon`, `ui/app.js: iconKey` |
| I3 | Library icons are identified as `l-<lucide name>` and take the color of their group. Built-in icons take precedence when names collide. | `ui/icon-library.js`, `ui/icons.js`, `ui/app.js: colorOf` |

### 4.7 Tray

| # | Rule | Implemented in |
| --- | --- | --- |
| T1 | Left click (button release) on the tray icon shows the window, or hides it if it is visible and not minimized. | `lib::toggle_main` |
| T2 | The tray menu lists pinned devices as check items (checked = on). A busy device shows "(working…)" and cannot be clicked; a protected device that is on shows "(protected)" and cannot be clicked; a disconnected device shows "(not connected)". Without pins, a disabled hint item is shown. Then: *Open USB Toggle*, *Refresh devices*, *Quit*. | `lib::build_tray_menu` |
| T3 | The tooltip says how many **connected** devices are off. | `lib::update_tray` |
| T4 | The tray icon is the frame of `assets/tray_icon.ico` that is the smallest one at least 16 × the primary monitor's scale factor pixels wide, falling back to the largest frame, then to the window icon (also when the file cannot be read; the error is logged). | `lib::tray_icon`, `ico::pick_frame` |

---

## 5. Data models

The full field-level description is in `data_models.txt`. Summary:

| Model | Where | Purpose |
| --- | --- | --- |
| `Device` | `devices.rs` | One physical USB device as shown to the user (serialized to the window) |
| `RawNode` | `devices.rs` | Raw data for one device node read from Windows |
| `NodeInfo` | `classify.rs` | Class, names, service and IDs of a node or function |
| `Category` | `classify.rs` | Device type enumeration |
| `Snapshot` | `lib.rs` | Device list + busy IDs + scan time + scan error (IPC payload) |
| `AppInfo` | `lib.rs` | Version, elevation, settings, autostart, paths, portable flag (IPC payload) |
| `ToggleResult` | `lib.rs` | Result of a switch operation (IPC payload) |
| `Core` | `lib.rs` | Process-wide application state |
| `Config`, `Settings`, `DeviceSettings` | `config.rs` | Persisted preferences (`config.json`) |
| `ActionError`, `ActionOutcome` | `control.rs` | Result of an enable/disable call |
| `AutostartState` | `autostart.rs` | Whether and how the app starts with Windows |
| Front-end `state` | `ui/app.js` | Window state: devices, busy/pending sets, filter, search, expanded rows |

---

## 6. Flows

### 6.1 Startup

1. `main()` → `run()`.
2. Logger initialized; if started with `--wait-for-pid`, wait for that process.
3. Configuration loaded (with migration and error handling); elevation detected; start-hidden
   decided.
4. Tauri builder: single-instance plugin, shared `Core`, command handlers.
5. `setup`: create the main window (hidden; portable WebView2 folder if applicable), build the
   tray icon, apply the theme, register the close handler, arm the 4-second show fallback, start
   the first scan on a thread, start `notify::watch`.
6. The page loads, renders a skeleton, applies the cached theme, subscribes to events, calls
   `ui_ready` after two animation frames (the back end shows the window), then calls `get_info`
   and `get_devices(force=false)`.

### 6.2 Refresh

`refresh()` → `devices::scan()` (two SetupAPI enumerations, property reads, tree walk) →
`assemble()` → `apply_config()` (names, pins, protection, sort) → store in `Core` → `publish()` → emit
`devices` + rebuild tray menu. Triggered by: first scan, `get_devices(force=true)` (refresh
button, F5, Ctrl+R), the tray's *Refresh devices*, device-change notifications, and every
switch operation.

### 6.3 Switching a device

Shown in `diagrams/sequence.mmd`. Window: optional confirmation → mark pending → invoke
`set_device_enabled` → back end marks busy, checks elevation, calls `control::set_enabled`,
clears busy, rescans, verifies → window applies the returned snapshot and shows a toast (error
with *Restart as admin* when access is denied; success with *Undo*; or "restart needed").

### 6.4 Device change

Windows calls the `CM_Register_Notification` callback → the callback sends on a channel → the
debounce thread waits for 350 ms of quiet → `refresh()`. If registration fails, the thread polls
every 4 seconds instead.

### 6.5 Renaming, pinning, protecting and choosing an icon

Window invokes `rename_device` / `set_pinned` / `set_locked` / `set_icon` → configuration updated and saved →
`reapply_config()` (no hardware rescan) → `publish()`. Pinning also adds the device to the tray
menu.

### 6.7 Changing settings

*Settings* opens the dialog with a copy of the current settings (the draft). Switches and the theme picker
change only the draft (the theme is previewed). *Save* calls `save_settings` and, when *Start with Windows*
changed, `set_autostart`; on success the dialog closes with a "Settings saved" toast. Leaving with unsaved
changes asks to *Save* or *Discard*.

### 6.6 Restart as administrator

Window invokes `restart_as_admin` → `ShellExecuteW("runas", exe, "--wait-for-pid <pid> [--tray]")`
→ on consent the current process exits → the new elevated process waits for the old PID, then
starts normally (the single-instance lock is free by then).

---

## 7. Diagram references

| Diagram | File | Shows |
| --- | --- | --- |
| Architecture | `diagrams/architecture.mmd` | Components, modules and the Windows facilities they use |
| Data flow | `diagrams/dataflow.mmd` | From Windows device data to the window and tray; user actions back to Windows |
| Classes | `diagrams/classes.mmd` | Data structures and their relationships |
| Sequence | `diagrams/sequence.mmd` | Switching a device off from the window, including confirmation and refresh |

---

## 8. Known limitations

- Built-in detection (D7) and panel detection (D9) depend on the motherboard firmware's ACPI
  description of its ports. Some boards report every port as "back", or nothing at all; devices
  on internal motherboard headers may be reported as removable.
- A USB 3 hub contains a USB 2 hub and a USB 3 hub, so Windows (and the app) list it as two
  hubs.
- Empty ports are not listed: the app lists devices, and Windows cannot disable a port, only a
  device or a whole hub.
- Disabling a device unloads its driver; the port may keep supplying power.
- On first start, the background scan and the window's `get_devices(force=false)` call can both
  scan once; the second waits for the first (`scan_lock`) and repeats it.
- Device-change notifications cover all device instances on the system, so non-USB changes also
  trigger a (debounced, cheap) rescan.
- The tray icon size is chosen once at startup from the primary monitor's scale.

---

## 9. File analysis

Each tracked file is described below with its purpose, a functional summary, technical details,
business logic and dependencies. Binary assets are described by role.

## 9.1 Project root, build and installer

## File: /.gitignore

### Purpose
Keeps build output, dependencies, logs and editor files out of version control. Configuration layer.

### Summary
Ignores `node_modules/`, `dist/`, `build/`, `target/`, `src-tauri/target/`, `src-tauri/gen/schemas/`,
`.env*` (except `.env.example`), `*.log`, OS files (`.DS_Store`, `Thumbs.db`, `Desktop.ini`), IDE folders
(`.idea/`, most of `.vscode/`), temporary files (`*.tmp`, `*.temp`, `*.swp`, `*.swo`, `*.bak`), coverage and
test output, and `package-lock.json`.

### Technical Details
Uses CRLF line endings. `Cargo.lock` is intentionally **not** ignored so release builds are reproducible.
`dist/` is ignored because `build.bat` writes the installer and portable ZIP there.

### Business Logic
None.

### Dependencies
None.

## File: /README.md

### Purpose
User guide: how to install and use the app, in plain language.

### Summary
Explains what turning a device off means, installation (installer and portable), the administrator
requirement, reading the grouped list and what is hidden by default, switching devices, protection, renaming,
pinning and icons, the tray menu, every setting with its default (and the Save/Discard behavior), keyboard
shortcuts, changing the app icon (the `assets` folder, `set-icons.bat`), a troubleshooting table, where files are stored in
installed and portable mode, and a short build section that points to `specs/`.

### Technical Details
Markdown. Installed by the Inno Setup script into the program folder.

### Business Logic
Describes rules P7–P9, P16, S0, S8, V1–V5 and I1 in user terms.

### Dependencies
Referenced by `installer/usbToggle.iss` (`[Files]`).

## File: /build.bat

### Purpose
One-step release build for Windows: compiles the executable and produces the portable ZIP and the
Inno Setup installer. Build layer.

### Summary
Inputs: the source tree. Outputs in `dist\`: `usbToggle-<version>-portable\` (folder),
`usbToggle-<version>-portable.zip`, and `usbToggle-<version>-setup.exe` when Inno Setup is installed.
Exit code 0 on success, 1 on any failure.

### Technical Details
1. Checks that `cargo` and `npm` are on `PATH`.
2. Reads `version` from `src-tauri\tauri.conf.json` with PowerShell `ConvertFrom-Json`.
3. Runs `npm install` when `node_modules` is missing (installs `@tauri-apps/cli`).
4. Runs `npx tauri build --no-bundle` (Tauri's own MSI/NSIS bundling is not used).
5. Recreates `dist\usbToggle-<version>-portable\`, copies `usbToggle.exe` and `installer\portable.txt`,
   and zips the folder with `Compress-Archive -Force`.
6. Looks for `ISCC.exe` on `PATH`, then in `%ProgramFiles(x86)%\Inno Setup 6`, `%ProgramFiles%\Inno Setup 6`
   and `%LOCALAPPDATA%\Programs\Inno Setup 6`. If found, compiles `installer\usbToggle.iss` with
   `/DAppVersion`, `/DSourceExe` and `/DOutputDir`; otherwise prints the `winget install JRSoftware.InnoSetup`
   hint and continues.
Written with CRLF line endings; uses `goto` labels to avoid parenthesis-parsing issues with
`%ProgramFiles(x86)%`.

### Business Logic
The portable package always contains `portable.txt`, which activates portable mode (rule P7).
A missing Inno Setup installation is not an error.

### Dependencies
Rust toolchain, Node.js/npm, `@tauri-apps/cli`, PowerShell, optionally Inno Setup 6.3+;
`src-tauri/tauri.conf.json`, `installer/portable.txt`, `installer/usbToggle.iss`.

## File: /package.json

### Purpose
npm manifest that provides the Tauri CLI. Build layer.

### Summary
Private package `usb-toggle` 1.1.0. Scripts: `tauri` (raw CLI), `dev` (`tauri dev`), `build`
(`tauri build`), `icons:library` (regenerates `ui/icon-library.js`). Dev dependencies: `@tauri-apps/cli`
`^2`, `lucide-static` `1.68.0`.

### Technical Details
There is no JavaScript build step; the front end is served as-is from `ui/`. `package-lock.json` is ignored.

### Business Logic
None.

### Dependencies
`@tauri-apps/cli`.

## File: /set-icons.bat

### Purpose
One-step tool for replacing the application icons. Build/tooling layer.

### Summary
Usage: `set-icons.bat image.png` (one image for everything; an image can also be dropped onto the file),
`set-icons.bat app.png tray.png` (separate tray image), `set-icons.bat --reset` (back to the built-in icons),
`set-icons.bat` (shows help, changes nothing). Pauses at the end so the result stays visible when started by
double-click or drag-and-drop.

### Technical Details
Builds the argument list for `tools/set-icons.ps1` (`-AppImage`, `-TrayImage` or `-Reset`) only for
arguments that are present, then runs it with `powershell -NoProfile -ExecutionPolicy Bypass -File`.
Exit code 1 on failure. CRLF line endings.

### Business Logic
Implements rule I1.

### Dependencies
`tools/set-icons.ps1`, Windows PowerShell.

## File: /tools/set-icons.ps1

### Purpose
Makes `assets/app_icon.ico`, `assets/tray_icon.ico` and `assets/header_logo.png` from a PNG or SVG image.
Build/tooling layer.

### Summary
With no image and no `-Reset` it prints usage and exits without changing anything. With `-AppImage` it
creates all three files from that image (the tray from `-TrayImage` when given). With `-Reset` it rebuilds
them from the built-in artwork in `assets/source/`.

### Technical Details
- Parameters: `-AppImage`, `-TrayImage`, `-Reset`.
- `Test-Image` validates the extension (PNG or SVG), that a PNG is square, and warns below 256 px. An `.ico`
  is refused with a hint to copy it into `assets\` by hand.
- `Invoke-TauriIcon` runs `npx tauri icon <image> -o <dir> -p <size> ...` (installs the CLI with
  `npm install` on first use) to rasterize the image into `<size>x<size>.png` files in a temporary folder.
- `Write-Ico` writes a multi-size ICO whose frames are PNG images (ICONDIR + 16-byte ICONDIRENTRY records,
  width/height 0 for 256 px).
- App icon frames: 16, 20, 24 from the small image, 32, 40, 48, 64, 128, 256 from the app image. Tray frames:
  16, 20, 24, 32, 40, 48, 64. Header logo: the 256 px render.
- Everything is rendered in a temporary folder and copied into `assets\` only at the end, so a failure leaves
  the current icons untouched. The temporary folder is always removed.

### Business Logic
Implements rule I1.

### Dependencies
Windows PowerShell 5.1+, `System.Drawing`, Node.js/npm, `@tauri-apps/cli`; `assets/source/*.svg`.

## File: /tools/build-icon-library.mjs

### Purpose
Generates `ui/icon-library.js` from the pick list and the `lucide-static` package. Build/tooling layer.

### Summary
Run with `npm run icons:library`. Reads `tools/icon-library.txt`, loads each listed icon's SVG and search
tags from `lucide-static`, and writes a self-contained script with the icon bodies, keywords, groups and the
Lucide license. Warns about names that do not exist in the installed Lucide version.

### Technical Details
Node.js ES module. Group lines are `## Label | color`; `#` lines are comments; other lines are icon names.
SVG bodies are whitespace-collapsed; keywords are limited to printable ASCII and 120 characters. Duplicate
names are skipped.

### Business Logic
Implements rule I3.

### Dependencies
Node.js, `lucide-static` (dev dependency, pinned), `tools/icon-library.txt`.

## File: /tools/icon-library.txt

### Purpose
The curated list of library icons offered in the device icon picker.

### Summary
16 groups (Input, Audio, Video and displays, Storage, Phones and computers, Connections, Power, Office,
Lights and climate, Security, Vehicles and health, Tools, Labels and shapes, People and places, Status,
Tech) with 239 Lucide icon names. Each group names a category color used for its icons.

### Technical Details
Plain text; format described in its header.

### Business Logic
Defines the contents of the icon library (rule I3).

### Dependencies
Read by `tools/build-icon-library.mjs`.

## File: /installer/usbToggle.iss

### Purpose
Inno Setup 6 script that builds the Windows installer. Deployment layer.

### Summary
Produces `usbToggle-<version>-setup.exe` that installs `usbToggle.exe` (and `README.md`) to
`{autopf}\USB Toggle`, adds a Start menu shortcut and an optional desktop shortcut, can launch the app at
the end, and provides an uninstaller.

### Technical Details
- Preprocessor defines with defaults: `AppVersion` (`0.0.0`), `SourceExe`
  (`..\src-tauri\target\release\usbToggle.exe`), `OutputDir` (`..\dist`).
- `[Setup]`: fixed `AppId` `{6F1D2A63-8C4B-4E1F-9B0A-7D3C2E5A9F41}`, `PrivilegesRequired=admin`,
  `ArchitecturesAllowed`/`ArchitecturesInstallIn64BitMode=x64compatible` (Inno 6.3+), `MinVersion=10.0`,
  modern wizard, LZMA2 ultra solid compression, `CloseApplications=no`, setup icon from `assets\app_icon.ico`.
- `[UninstallRun]`: deletes the scheduled task `usbToggle` and the `HKCU\...\Run\usbToggle` value (hidden).
- `[Code]`:
  - `HasWebView2` checks the WebView2 client key
    `SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` (`pv` value) in HKLM 32/64
    and HKCU.
  - `InitializeSetup` offers to open the WebView2 download page when it is missing (setup continues either way).
  - `StopApp` runs `taskkill /F /IM usbToggle.exe`; called from `PrepareToInstall` and `InitializeUninstall`.
  - `CurUninstallStepChanged` (post-uninstall, interactive only) asks whether to delete settings and removes
    `%APPDATA%\usbToggle`, `%LOCALAPPDATA%\usbToggle` and `%LOCALAPPDATA%\com.usbtoggle.app`.

### Business Logic
The app keeps running in the tray, so it is force-closed before files are replaced or removed. Uninstall
removes "Start with Windows" (rule P9). User data is kept unless the user agrees to delete it.

### Dependencies
Inno Setup 6.3+, `assets/app_icon.ico`, `README.md`, the built executable; invoked by `build.bat`.

## File: /installer/portable.txt

### Purpose
Marker and explanation file shipped in the portable package. Deployment layer.

### Summary
Its presence next to `usbToggle.exe` switches the app to portable mode; its text explains that settings,
logs and browser data are kept in the `data` folder, how to leave portable mode (delete the file), and that
switching devices needs administrator rights and WebView2.

### Technical Details
CRLF text. Detected by `paths::portable_dir()` through the constant `PORTABLE_MARKER`.

### Business Logic
Implements rule P7.

### Dependencies
Copied by `build.bat`; read (existence only) by `src-tauri/src/paths.rs`.

## 9.2 Rust project configuration

## File: /src-tauri/.gitignore

### Purpose
Cargo-generated ignore file for the Rust crate.

### Summary
Ignores `/target/` and `/gen/schemas` (Tauri-generated capability schemas).

### Technical Details
Complements the project-level `.gitignore`.

### Business Logic
None.

### Dependencies
None.

## File: /src-tauri/Cargo.toml

### Purpose
Rust crate manifest. Build layer.

### Summary
Package `usb-toggle` 1.1.0 (edition 2021, `rust-version` 1.77.2). Library target `usb_toggle_lib`
(`staticlib`, `cdylib`, `rlib`) and binary target `usbToggle` (`src/main.rs`).

### Technical Details
- Dependencies: `serde` (derive), `serde_json`, `tauri` 2 (features `tray-icon`, `image-png`, `image-ico`),
  `tauri-plugin-single-instance` 2, `chrono`.
- Windows-only dependency: `windows-sys` 0.61 with features `Win32_Foundation`,
  `Win32_Devices_DeviceAndDriverInstallation`, `Win32_Devices_Properties`, `Win32_Security`,
  `Win32_System_Threading`, `Win32_UI_Shell`, `Win32_UI_WindowsAndMessaging`.
- Build dependency: `tauri-build` 2.
- Release profile: `codegen-units = 1`, `lto = true`, `opt-level = "s"`, `panic = "abort"`, `strip = true`
  (small executable).

### Business Logic
None.

### Dependencies
crates.io.

## File: /src-tauri/Cargo.lock

### Purpose
Pins exact versions of all Rust dependencies for reproducible builds.

### Summary
Generated by Cargo; about 116 KB. Pins, among others, `tauri` 2.12.x, `windows-sys` 0.61.2,
`tauri-plugin-single-instance` 2.5.x.

### Technical Details
Committed on purpose (removed from `.gitignore`). Updated with `cargo update`.

### Business Logic
None.

### Dependencies
`Cargo.toml`.

## File: /src-tauri/build.rs

### Purpose
Cargo build script.

### Summary
Copies `assets/header_logo.png` to `ui/logo.png` (the window serves `ui/`), then calls `tauri_build::build()`,
which reads `tauri.conf.json`, embeds the application icon (`assets/app_icon.ico`) and version resource into
the Windows executable, generates the context used by `tauri::generate_context!()`, and generates
permission schemas.

### Technical Details
One function, `main()`. The logo is copied only when its content differs, so the build does not loop on
its own output. `cargo:rerun-if-changed` is emitted for the three files in `assets/`. A missing logo or a
failed copy is reported as a `cargo:warning`, not an error. A missing `app_icon.ico` fails the build
(`tauri-build` requires it for the Windows resource).

### Business Logic
None.

### Dependencies
`tauri-build`, `tauri.conf.json`, `assets/app_icon.ico`, `assets/header_logo.png`.

## File: /src-tauri/tauri.conf.json

### Purpose
Tauri application configuration.

### Summary
- `productName` `usbToggle`, `mainBinaryName` `usbToggle`, `version` `1.1.0`, `identifier` `com.usbtoggle.app`.
- `build.frontendDist` `../ui` (served as embedded assets; no dev server, no build command).
- `app.withGlobalTauri: true` (exposes `window.__TAURI__` to `app.js`), `security.csp: null`.
- One window definition, label `main`, title "USB Toggle", 620×760 (minimum 440×520), centered,
  resizable, `visible: false`, `create: false`.
- `bundle.active: false`; `bundle.icon` lists only `../assets/app_icon.ico`, which `tauri-build` embeds as the
  executable's icon resource (the window and taskbar icon on Windows are read from it).

### Technical Details
`create: false` means Tauri does not create the window automatically; `lib.rs` builds it from this
definition with `WebviewWindowBuilder::from_config` so it can set a portable WebView2 data folder.
`visible: false` prevents a white flash; the window is shown by `ui_ready`.

### Business Logic
Implements part of rule P14 (hidden until painted).

### Dependencies
Read by `tauri-build` at compile time and by `lib.rs` at run time (`app.config()`).

## File: /src-tauri/capabilities/default.json

### Purpose
Tauri 2 permission set for the main window.

### Summary
Grants `core:default` (events, window basics, app info) to the window labelled `main`.

### Technical Details
The app's own commands do not need explicit permissions. The `$schema` points at the generated
`gen/schemas/desktop-schema.json`.

### Business Logic
None.

### Dependencies
Tauri capability system.

## 9.3 Rust back end

## File: /src-tauri/src/main.rs

### Purpose
Binary entry point.

### Summary
Calls `usb_toggle_lib::run()`. In release builds, `windows_subsystem = "windows"` prevents a console
window from opening.

### Technical Details
`#![allow(non_snake_case)]` silences the lint caused by the binary being named `usbToggle`.

### Business Logic
None.

### Dependencies
`lib.rs`.

## File: /src-tauri/src/lib.rs

### Purpose
Application core: process-wide state, IPC commands, tray icon and menu, window lifecycle and startup.
Application layer.

### Summary
- Holds the shared state `Core` (behind `Arc`, registered with Tauri's state manager).
- Exposes 13 IPC commands to the window (see `api_reference.md`).
- Emits three events: `devices` (snapshot), `busy` (IDs with an operation in progress), `toast`
  (messages for tray-initiated failures).
- Builds and updates the tray icon, menu and tooltip.
- Creates the main window, applies the theme, handles close-to-tray, and shows the window when the page
  is ready.

### Technical Details
- **State:** `Core { store, config, config_error, devices, scan_error, scanned_at, scanned, busy,
  scan_lock, op_lock, elevated, start_hidden }`. `UI_READY` (static `AtomicBool`) records that the page has
  reported in once.
- **IPC payloads:** `Snapshot`, `AppInfo`, `ToggleResult` (serde, camelCase).
- **State functions:**
  - `auto_lock(device, settings)` — the automatic protection rule (keyboards and mice).
  - `apply_config(devices, cfg)` — applies custom names, pins, protection and icons, then sorts by name and ID.
  - `snapshot(core)` — copies the current list, sorted busy IDs, scan time and scan error.
  - `refresh(app)` — scans under `scan_lock`, applies the config, stores the result (or the error), sets the
    scan time and `scanned`, then `publish`.
  - `reapply_config(app)` — re-applies names/pins/protection to the cached list without scanning, then `publish`.
  - `publish(app)` — emits `devices` and rebuilds the tray. Must be called without holding core locks.
  - `set_busy(app, id, busy)` — updates the busy set and emits `busy`.
  - `toggle_device(app, id, enable)` — refuses to turn off a protected device (`locked`); busy on; under `op_lock`, calls `control::set_enabled` if elevated
    or returns `accessDenied`; busy off; logs; `refresh`; verifies the state and builds a `ToggleResult`.
- **Tray:** `update_tray` (menu + tooltip), `build_tray_menu`, `on_menu_event` (`show`, `quit`,
  `refresh`, `dev:<instance id>`), `tray_icon(scale)` (`assets/tray_icon.ico` built in with `include_bytes!`, frame chosen by `ico::pick_frame`).
- **Window:** `show_main` (unminimize, show, focus), `toggle_main`, `apply_theme` (`light`/`dark`/system).
- **Commands:** `get_info`, `get_devices`, `set_device_enabled`, `rename_device`, `set_pinned`,
  `set_locked`, `set_icon` (validates the icon name), `save_settings` (also re-applies device state), `set_autostart`, `restart_as_admin`, `ui_ready`, `open_folder`, `quit`. Commands that touch
  Windows or disk are `async` and run through `blocking()` (`spawn_blocking`).
- **`run()`:** logger init → `wait_for_previous_instance` → load config → detect elevation → decide
  start-hidden → build `Core` → Tauri builder (single-instance plugin, state, handlers) → `setup` (create
  window from config with optional `data_directory`, build tray, apply theme, register close handler, 4-second
  show fallback, first scan thread, `notify::watch`) → `run`. A fatal Tauri error is logged.
- **Error handling:** commands return `Result<_, String>`; `toggle_device` never returns an error to the
  window, it returns `ok: false` with a code and message. Mutex poisoning is treated as fatal (`unwrap`).
- **Concurrency:** menu handlers run on the main thread and immediately move work to a new thread; locks
  are released before any Tauri call that dispatches to the main thread.

### Business Logic
Implements rules D12, N5, S0–S3, S6, S9, P8, P11–P14, I2 and T1–T4.

### Dependencies
`autostart`, `classify` (via `devices`), `config`, `control`, `devices`, `elevation`, `ico`, `logger`, `notify`,
`paths`, `win` (indirectly); crates `tauri`, `tauri-plugin-single-instance`, `serde`, `serde_json`,
`chrono`.

## File: /src-tauri/src/devices.rs

### Purpose
USB device discovery: reads device nodes from Windows and turns them into `Device` records. Domain layer.

### Summary
`scan()` returns every physical USB device (connected or remembered) with its name, type, state,
problem code, hub, port, panel, built-in flag, last-seen time and function names. Custom names, pins and protection
are applied later by `lib.rs`.

### Technical Details
- **Types:** `Device` (serialized, camelCase), `RawNode` (internal), constant `MACHINE_CONTAINER`.
- **Pure helpers:** `assemble(nodes)`, `is_built_in`, `panel_of`, `device_part`, `is_interface`,
  `is_root_hub`, `is_top_level`, `vidpid_key`.
- **Windows implementation (`mod imp`, `cfg(windows)`):**
  - `DevInfoSet` — owns an `HDEVINFO`, destroyed on drop.
  - `open_set(flags)` — `SetupDiGetClassDevsW(NULL, "USB", NULL, DIGCF_ALLCLASSES | flags)`.
  - `enum_devices`, `instance_id`, `registry_bytes/string/multi/dword`, `split_multi`, `bytes_to_wide`.
  - `property_bytes/string/filetime_ms` — `SetupDiGetDevicePropertyW` with retry on a larger buffer;
    FILETIME is converted to Unix milliseconds.
  - `devnode_status` — `CM_Get_DevNode_Status`; problem code only when `DN_HAS_PROBLEM`.
  - `cm_device_id`, `cm_registry`, `cm_node_info`, `cm_parent`, `descendants` — CfgMgr32 tree walk (stops below Bluetooth radios).
  - `scan()` — enumeration 1 (`DIGCF_PRESENT`) builds the set of present instance IDs; enumeration 2 (all)
    reads class, description, friendly name, bus-reported description, service, hardware IDs, compatible IDs,
    config flags, base container ID, capabilities, location information and `_PLD` for every node; for present
    nodes also status/problem, parent ID, descendants (not for interfaces or root hubs) and last arrival; for
    absent nodes last removal (or arrival). Then `assemble`.
- **Properties read:** `SPDRP_CLASS`, `SPDRP_DEVICEDESC`, `SPDRP_FRIENDLYNAME`, `SPDRP_SERVICE`,
  `SPDRP_HARDWAREID`, `SPDRP_COMPATIBLEIDS`, `SPDRP_CONFIGFLAGS`, `SPDRP_BASE_CONTAINERID`,
  `SPDRP_CAPABILITIES`, `SPDRP_LOCATION_INFORMATION`, `DEVPKEY_Device_BusReportedDeviceDesc`,
  `DEVPKEY_Device_PhysicalDeviceLocation`, `DEVPKEY_Device_LastArrivalDate`, `DEVPKEY_Device_LastRemovalDate`.
- **Error handling:** failing to open a device set returns `Err` with the OS error; missing properties are
  treated as empty/absent. Enumeration is capped at 20,000 entries; descendant walks at depth 4 / 64 nodes.
- **Non-Windows:** `scan()` returns an "only available on Windows" error.
- **Tests:** `groups_interfaces_and_hubs` covers interface folding, root-hub exclusion, hub linking,
  built-in detection, built-in hub chips, panel inheritance and naming.

### Business Logic
Implements rules D1–D11 and C1, and calls the naming and classification rules in `classify.rs`.

### Dependencies
`classify`, `win`; `windows-sys` (SetupAPI, CfgMgr32, Properties, Foundation); `serde`. Used by `lib.rs` and
`control.rs` (which reuses `DevInfoSet`, `registry_dword`, `devnode_status`).

## File: /src-tauri/src/classify.rs

### Purpose
Pure functions that turn raw PnP data into names, types and locations. No Win32 calls; unit-tested on any
platform. Domain layer.

### Summary
Decides the device `Category`, the display name, the human label for each function, parses VID/PID from an
instance ID, the port number from location information, the panel from a `_PLD` buffer, and maps common USB
vendor IDs to company names.

### Technical Details
- **Types:** `NodeInfo` (class, description, friendly name, bus description, service, hardware IDs,
  compatible IDs); `Category` enum (15 values, serialized lowercase) with `label()`.
- **Constants:** `GENERIC_NAMES` (names never used as a display name), `SKIP_FUNCTIONS` (function names not
  worth listing).
- **Functions:** `is_generic`, `ids_upper`, `has_usb_class` (matches `CLASS_xx` followed by end or `&`, never
  `SUBCLASS_xx`), `is_hub`, `categorize`, `clean`, `best_of`, `display_name`, `function_label`, `parse_vid_pid`
  (4 hex digits after `VID_`/`PID_`), `parse_pld_panel` (bits 67–69 of the `_PLD` buffer), `parse_port`
  (`Port_#0003...` → 3), `vendor_name` (about 80 vendor IDs).
- **Tests:** `vid_pid`, `panel`, `port`, `categories`, `bluetooth_on_a_wifi_combo_card`, `names`.

### Business Logic
Implements rules C2–C4, D6, D9 (parsing part) and N1–N4.

### Dependencies
`serde` (for `Category`). Used by `devices.rs`.

## File: /src-tauri/src/control.rs

### Purpose
Enables or disables one device instance. Domain layer.

### Summary
`set_enabled(instance_id, enable)` returns `Ok(ActionOutcome { needs_restart })` or
`Err(ActionError { code, message })`.

### Technical Details
- Opens the device with `SetupDiCreateDeviceInfoList` + `SetupDiOpenDeviceInfoW`.
- Connected device (`devnode_status` succeeds) → `change_live`: `SetupDiSetClassInstallParamsW` with
  `SP_PROPCHANGE_PARAMS` (`DICS_ENABLE`/`DICS_DISABLE`, `DICS_FLAG_GLOBAL`) and
  `SetupDiCallClassInstaller(DIF_PROPERTYCHANGE)`; enabling also tries `DICS_FLAG_CONFIGSPECIFIC`. Reads
  `SP_DEVINSTALL_PARAMS_W.Flags` for `DI_NEEDREBOOT`/`DI_NEEDRESTART`.
- Disconnected device → `change_offline`: reads `SPDRP_CONFIGFLAGS`, sets or clears `CONFIGFLAG_DISABLED`,
  writes it back with `SetupDiSetDeviceRegistryPropertyW` (only if it changed).
- `map_error` maps `ERROR_ACCESS_DENIED` → `accessDenied`, `ERROR_NOT_DISABLEABLE` (0xE0000231) →
  `notDisableable`, `ERROR_NO_SUCH_DEVINST` (0xE000020B) → `missing`, `ERROR_IN_WOW64` (0xE0000235) → `failed`
  with a 64-bit hint, others → `failed` with the system message or "SetupAPI error 0x…".
- Non-Windows stub returns `failed`.

### Business Logic
Implements rules S4, S5 and S7 and the restart detection part of S6.

### Dependencies
`devices::imp` (`DevInfoSet`, `registry_dword`, `devnode_status`), `win` (`wide`, `last_error`),
`windows-sys`, `serde`. Used by `lib.rs`.

## File: /src-tauri/src/config.rs

### Purpose
User preferences: settings, custom names, pins and lock choices; loading, migrating, backing up and saving
`config.json`. Domain layer.

### Summary
`Store::load()` returns the configuration and an optional message for the user; `Store::save()` writes it
atomically. `Config::device()` and `Config::update_device()` read and change per-device entries.

### Technical Details
- **Types:** `Settings` (theme, closeToTray, startMinimized, showDisconnected, showHubs, portsOnly,
  confirmRisky, hideProblems, protectInput), `DeviceSettings` (name, pinned, locked, icon), `Config` (schemaVersion, settings, devices map), `Store`
  (path). All serde camelCase with `default`.
- **Constant:** `SCHEMA_VERSION = 3`.
- **Load:** not found → defaults; read error → defaults + message; parse OK and schema ≤ 3 → migrate (schema
  < 3 resets portsOnly/showHubs/showDisconnected to defaults) and set schema to 3; schema > 3 → defaults +
  message; parse error → backup to `config.<timestamp>.bak.json` + defaults + message.
- **Save:** creates the folder, writes pretty JSON to `config.json.tmp`, renames it over `config.json`.
- **Tests:** `old_or_partial_files_load`, `v2_files_get_the_new_list_defaults_once`,
  `empty_device_entries_are_dropped`.

### Business Logic
Implements rules P1–P6.

### Dependencies
`paths` (`config_dir`), `serde`, `serde_json`, `chrono`. Used by `lib.rs`.

## File: /src-tauri/src/autostart.rs

### Purpose
"Start with Windows". Domain layer.

### Summary
`state()` reports `{ enabled, elevated }`; `set(enabled, elevated)` creates or removes the startup entry
and returns the new state.

### Technical Details
- Constants: `TASK_NAME = "usbToggle"`, `RUN_KEY = HKCU\Software\Microsoft\Windows\CurrentVersion\Run`,
  `RUN_VALUE = "usbToggle"`, `AUTOSTART_ARG = "--autostart"`.
- `task_exists` (`schtasks /Query /TN usbToggle`), `run_value_exists` (`reg query`).
- `create_task(exe)` writes a Task Scheduler XML (logon trigger for `USERDOMAIN\USERNAME` with a 3-second
  delay, `InteractiveToken`, `HighestAvailable`, `IgnoreNew`, no battery limits, `PT0S` time limit,
  priority 7, action `exe --autostart`) as UTF-16 LE with BOM to `%TEMP%`, runs `schtasks /Create /F /XML`,
  then deletes the temporary file. Values are XML-escaped.
- `delete_task`, `set_run_value(Some(exe) | None)`.
- All child processes are started without a console window (`win::hidden_command`).
- Errors are returned as readable strings (including `schtasks`/`reg` stderr).
- Non-Windows stubs.

### Business Logic
Implements rules P9 and P10.

### Dependencies
`win::hidden_command`, `schtasks.exe`, `reg.exe`, `serde`. Used by `lib.rs`.

## File: /src-tauri/src/elevation.rs

### Purpose
Administrator detection and "restart as administrator". Platform layer.

### Summary
`is_elevated()` checks the process token; `relaunch_elevated(extra_args)` starts an elevated copy;
`wait_for_previous_instance()` lets that copy wait for the old process.

### Technical Details
- `is_elevated`: `OpenProcessToken(TOKEN_QUERY)` + `GetTokenInformation(TokenElevation)`.
- `relaunch_elevated`: `ShellExecuteW(NULL, "runas", <current exe>, "--wait-for-pid <pid> <extra>", NULL,
  SW_SHOWNORMAL)`; success when the result is greater than 32; otherwise "Administrator permission was not
  granted."
- `wait_for_previous_instance`: parses `--wait-for-pid <pid>`, `OpenProcess(PROCESS_SYNCHRONIZE)`,
  `WaitForSingleObject(5000 ms)`.
- Non-Windows stubs (not elevated; relaunch unsupported).

### Business Logic
Implements rule P13; supplies the elevation flag used by S1 and P9.

### Dependencies
`win::wide`, `windows-sys` (Security, Threading, Shell, WindowsAndMessaging). Used by `lib.rs`.

## File: /src-tauri/src/ico.rs

### Purpose
Selects one frame from a multi-size `.ico` file. Platform layer (pure Rust, no Win32 calls).

### Summary
`pick_frame(ico, want)` returns a one-frame `.ico` holding the smallest frame that is at least `want`
pixels wide, or the largest frame when none is big enough, or `None` when the data is not a valid icon.

### Technical Details
Reads the ICONDIR header (reserved 0, type 1, count 1–256) and the 16-byte entries (width 0 means 256, data
size, data offset). Entries whose data lies outside the file are ignored. The result is a new header and a
single entry whose data offset is 22, followed by the frame's bytes, so Tauri's decoder (`image-ico` feature)
can read PNG- and BMP-encoded frames alike. Six unit tests cover frame choice, order independence, the 256 px
encoding, the result's validity, invalid files and damaged entries.

### Business Logic
Implements rule T4.

### Dependencies
Standard library only. Used by `lib.rs` (`tray_icon`).

## File: /src-tauri/src/notify.rs

### Purpose
Tells the app when devices change. Platform layer.

### Summary
`watch(on_change)` registers for Windows device notifications and calls `on_change` on a background
thread at most once per burst of events.

### Technical Details
- A static `OnceLock<Mutex<Sender<()>>>` lets the `extern "system"` callback signal a channel.
- `register()` calls `CM_Register_Notification` with `CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE` and
  `CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES`. The registration is never removed.
- `debounce_loop`: waits for an event (or 4 seconds when polling), then keeps reading until 350 ms pass with
  no event, then calls `on_change`.
- If registration fails, an error is logged and polling mode is used.

### Business Logic
Implements flow 6.4.

### Dependencies
`logger` (`log_error!`), `windows-sys` (CfgMgr32). Used by `lib.rs`.

## File: /src-tauri/src/paths.rs

### Purpose
Decides where the app keeps its files. Platform layer.

### Summary
`portable_dir()` returns `<exe dir>\data` when `portable.txt` exists next to the executable (computed once).
`config_dir()`, `log_dir()` and `webview_dir()` return the portable locations or the per-user defaults
(`%APPDATA%\usbToggle`, `%LOCALAPPDATA%\usbToggle\logs`, Tauri default). `is_portable()` reports the mode.

### Technical Details
Falls back to the temp directory when `APPDATA`/`LOCALAPPDATA` are not set. Uses `OnceLock` for the cached
result.

### Business Logic
Implements rule P7.

### Dependencies
Standard library only. Used by `config.rs`, `logger.rs`, `lib.rs`.

## File: /src-tauri/src/logger.rs

### Purpose
Minimal file logger. Platform layer.

### Summary
`LOGGER.init()` creates the log folder; `LOGGER.log(level, message)` appends
`[YYYY-MM-DD HH:MM:SS] [LEVEL] message`. The macros `log_info!` and `log_error!` format and log.

### Technical Details
Static `Logger { path: Mutex<Option<PathBuf>> }`. Before each write, if the file exceeds 1 MB it is renamed
to `usbToggle.old.log`. All I/O errors are ignored, so logging can never crash the app. `log_dir()` delegates
to `paths::log_dir()`.

### Business Logic
Implements rule P15.

### Dependencies
`paths`, `chrono`. Used by every module that logs (`lib`, `notify`).

## File: /src-tauri/src/win.rs

### Purpose
Small Win32 helpers shared by the Windows modules. Platform layer.

### Summary
`wide(s)` converts to a NUL-terminated UTF-16 buffer; `wide_to_string(buf)` reads a UTF-16 buffer up to the
first NUL and trims it; `last_error()` returns `GetLastError()`; `hidden_command(program)` creates a
`std::process::Command` with `CREATE_NO_WINDOW`.

### Technical Details
`last_error` and `hidden_command` are compiled only on Windows.

### Business Logic
None.

### Dependencies
`windows-sys` (Foundation, Threading). Used by `devices`, `control`, `elevation`, `autostart`.

## 9.4 User interface

## File: /ui/index.html

### Purpose
Static structure of the window. Presentation layer.

### Summary
Defines the header (logo, title, summary line, refresh and settings buttons), the administrator banner,
the configuration notice, the toolbar (search field with clear button and Ctrl F hint; All/On/Off filter),
the device list container, the toast area, the confirmation dialog and the Settings dialog.

### Technical Details
- Loads `styles.css`, then `icons.js`, `icon-library.js` and `app.js` with `defer`. No inline scripts.
- The Settings dialog has a fixed footer (`#settings-dirty`, `#settings-cancel`, `#settings-save`); the
  `#unsaved` dialog asks to *Discard* or *Save*.
- Elements referenced by ID from `app.js`: `summary`, `refresh`, `open-settings`, `banner`, `elevate`,
  `notice`, `search`, `search-clear`, `filter`, `list`, `toasts`, `confirm`, `confirm-title`,
  `confirm-text`, `confirm-dont-ask`, `confirm-ok`, `settings`, `theme`, `autostart`, `autostart-desc`,
  `settings-dirty`, `settings-cancel`, `settings-save`, `unsaved`, `unsaved-save`, `version`, `quit`.
- Settings switches use `data-setting` (`portsOnly`, `showDisconnected`, `showHubs`, `hideProblems`,
  `protectInput`, `confirmRisky`, `closeToTray`, `startMinimized`); folder buttons use `data-open-folder` (`config`, `logs`).
- Settings dialog layout: a fixed title bar (`.dialog-bar` with the close button) and a scrolling body
  (`.dialog-body`) inside `.dialog-scroll`.
- Switches are `<button role="switch">` with `aria-checked`; the filter and theme pickers are
  `role="radiogroup"` with `role="radio"` buttons.

### Business Logic
Settings texts describe rules V2–V4b, S8, P9 and P11.

### Dependencies
`styles.css`, `icons.js`, `icon-library.js`, `app.js`, `logo.png` (generated from `assets/header_logo.png` by `build.rs`).

## File: /ui/app.js

### Purpose
All front-end behavior: state, rendering, user interactions and communication with the back end.
Presentation layer.

### Summary
Loads the app information and device list, renders the grouped device list, handles switching (with
confirmation, progress and *Undo*), renaming, pinning, search, filtering, settings, autostart, the
administrator restart, keyboard shortcuts and toasts. Reacts to back-end events.

### Technical Details
- Wrapped in an IIFE in strict mode. If `window.__TAURI__` is missing, it shows a message and stops.
- **Bridge:** `invoke(cmd, args)` → `__TAURI__.core.invoke`; `listen(name, cb)` → `__TAURI__.event.listen`.
- **Constants:** `CATEGORY` (labels), `ICON_CHOICES` (22 built-in picker icons), `ICON_COLOR` (colors of the
  extra built-in icons), `PANEL` and `PANEL_ORDER`, `RISKY` (keyboard, mouse, input, hub,
  bluetooth), `PROBLEMS` (readable text for 21 Device Manager problem codes), `DEFAULT_SETTINGS`.
- **State object:** `loaded`, `devices`, `byId`, `busy` (from the back end), `pending` (ID → requested
  state, optimistic), `scannedAt`, `scanError`, `info`, `settings`, `filter`, `query`, `expanded`,
  `collapsed`, `refreshing`.
- **Local storage** (`usbToggle.` prefix, every access wrapped in try/catch): `theme` (avoids a flash of
  the wrong theme), `collapsed` (section state), `pinHintShown`.
- **Helpers:** `$`, `$$`, `h` (element builder), `iconSpan`, `hydrateIcons`, `ago` (relative time with
  `Intl.RelativeTimeFormat`), `hasProblem`, `isOn`, `isBusy`, `categoryLabel`, `panelLabel`, `vidPid`,
  `hubChildren`.
- **Toasts:** `toast(message, { kind, action, duration })` (at most 3 visible; 4 s, or 9 s for errors),
  `errorToast(message, code)` (adds *Restart as admin* for `accessDenied` when not elevated).
- **Data intake:** `applySnapshot`, `applyInfo`, `applyTheme`, `saveSettings` (optimistic, rolled back on
  error).
- **Actions:** `refresh` (minimum 450 ms spinner), `setEnabled` (refuses to turn off a protected device),
  `setLocked`, `setIcon`, `setPinned` (one-time hint about the tray),
  `rename`, `restartAsAdmin`, `copy` (Clipboard API with a `textarea` fallback), `confirmDisable`.
- **Rendering:** keyed and incremental. `rows` maps device ID → row elements; `sections` maps section key →
  section elements; `reconcile(container, nodes)` inserts, moves and removes nodes without rebuilding
  focused elements. Functions: `matches`, `passesFilter`, `metaParts`, `createRow`, `updateRow`, `fact`,
  `renderDetails` (rename field, icon picker, problem note, facts list), `renderIconPicker`, `iconButton`,
  `buildIconLibrary` (searchable library panel, built on first open), `protectionText`, `connectionText`, `toggleExpanded`,
  `getSection`, `getHiddenNote` (one note per hiding setting), `describeHidden`, `skeleton`, `emptyState`, `renderSummary`, `render`.
- **Filter/search:** `setFilter`, `setQuery`.
- **Settings (draft):** `openSettings` (copies the settings into `draft`), `settingsDirty`,
  `syncSettingsDialog` (switches, autostart description, *Start in the tray* availability, Save button and
  "Unsaved changes" note), `saveDraft`, `discardDraft`, `requestCloseSettings` (opens `#unsaved` when dirty).
- **Icons:** `autoIconKey`, `knownIcon`, `iconKey`, `colorOf`, `iconLabel`.
- **Wiring (`bind`):** buttons, search input and clear button, filter, settings controls, dialogs (backdrop
  click closes), keyboard shortcuts (Ctrl+F or `/` focus search; F5 or Ctrl+R refresh without reloading;
  Ctrl+P/S/U/G/J blocked; Escape clears search or collapses details), context menu blocked except in text
  fields or over a selection, and a 60-second timer that refreshes relative times.
- **`init`:** icons, cached theme and collapsed state, `bind`, first render (skeleton), event listeners
  (`devices`, `busy`, `toast`), `ui_ready` after two animation frames, then `get_info` and
  `get_devices(force=false)`.

### Business Logic
Implements rules S0 (display), S3 (display), S8, S10, P16, I2, I3 and V1–V9 (including V4b), and the window side of flows 6.1, 6.3, 6.5 and 6.6.

### Dependencies
`window.__TAURI__` (global API enabled by `withGlobalTauri`), `icons.js` (`window.icon`), `icon-library.js`
(`window.ICON_LIBRARY`), `index.html`
element IDs, back-end commands and events (see `api_reference.md`).

## File: /ui/styles.css

### Purpose
Visual design of the window. Presentation layer.

### Summary
Defines the light and dark themes, layout, buttons, banner, toolbar, list sections, device rows, switches,
details panel, empty and loading states, toasts, dialogs and settings rows.

### Technical Details
- Design tokens as CSS custom properties on `:root` (surface, border, text, accent, danger, warning colors;
  radii; shadows; one color per device category). Dark values are applied under
  `@media (prefers-color-scheme: dark)` when no explicit light theme is set, and under
  `:root[data-theme="dark"]`.
- Fonts: Segoe UI Variable (Text/Display) with system fallbacks; Cascadia Mono/Consolas for IDs.
- Category tiles use `color-mix()` to tint the background with the category color.
- Switch: 42×22 px pill with an animated knob; `aria-busy="true"` turns the knob into a spinner.
- Details panel expands with a `grid-template-rows: 0fr → 1fr` transition.
- Search field: a single focus ring on the container; the inner input draws none.
- Settings dialog: `.dialog-scroll` makes the title bar fixed and the body scroll.
- Responsive rule at 520 px stacks the toolbar; `prefers-reduced-motion` disables animations.

### Business Logic
None (presentation only).

### Dependencies
`index.html` structure and the classes created by `app.js`.

## File: /ui/icons.js

### Purpose
Inline SVG icon set. Presentation layer.

### Summary
Exposes `window.ICONS` (name → SVG markup) and `window.icon(name)`, which returns a built-in icon, else a
library icon (`l-<name>` from `icon-library.js`), else the USB icon.

### Technical Details
24×24 line icons drawn with `currentColor`, stroke width 1.8. Device icons: `usb`, `keyboard`, `mouse`,
`input`, `gamepad`, `audio`, `camera`, `storage`, `phone`, `bluetooth`, `network`, `printer`, `serial`,
`security`, `hub`, `other` (alias of `usb`); extra picker icons `mic`, `speaker`, `monitor`, `pen`, `card`,
`light`, `fan`. Interface icons: `refresh`, `settings`, `search`, `pin`,
`chevron`, `lock`, `unlock`, `shield`, `copy`, `x`, `alert`, `check`, `info`, `unplugged`.

### Business Logic
Device icon names match the `Category` values serialized by the back end.

### Dependencies
None. Used by `app.js` and by `data-icon` attributes in `index.html`.

## File: /ui/icon-library.js

### Purpose
Library of extra icons for the device icon picker. Presentation layer (generated).

### Summary
Exposes `window.ICON_LIBRARY` with `groups` (label, color, icons with key, name and keywords), `count`
(239), `has(key)`, `svg(key)` and `color(key)`. Keys are `l-<lucide name>`.

### Technical Details
Generated by `tools/build-icon-library.mjs`; do not edit by hand. Contains the Lucide license (ISC, with the
Feather MIT notice) in its header comment. Icon bodies are wrapped in the same SVG element as the built-in
icons (24×24, stroke width 1.8), so both sets look alike. About 73 KB.

### Business Logic
Implements rule I3.

### Dependencies
Loaded by `index.html` after `icons.js`; used by `icons.js` (`window.icon`) and `app.js` (picker).

## 9.5 Assets (icons)

## File: /assets/README.md

### Purpose
Explains the icon files and how to replace them.

### Summary
Tabulates what each icon file is used for and gives the two ways to change them: `set-icons.bat`, or
replacing the files by hand while keeping the names (with the sizes an `.ico` should contain).

### Technical Details
Markdown.

### Business Logic
Describes rule I1.

### Dependencies
None.

## File: /assets/app_icon.ico

### Purpose
The application icon: window title bar, taskbar, Start menu, the `.exe` file and the installer.

### Summary
Static asset; no executable behavior.

### Technical Details
ICO with PNG-encoded frames of 16, 20, 24, 32, 40, 48, 64, 128 and 256 px. The 16, 20 and 24 px frames
come from the simplified artwork (`source/app_icon_small.svg`: no USB mark, no "on" bar), the others from
`source/app_icon.svg`. Listed in `tauri.conf.json` (`bundle.icon`), which `tauri-build` embeds as the
executable's icon resource; on Windows the window and taskbar icon are read from that resource.

### Business Logic
None.

### Dependencies
`src-tauri/tauri.conf.json`, `src-tauri/build.rs` (rebuild trigger), `installer/usbToggle.iss`
(`SetupIconFile`). Written by `tools/set-icons.ps1`.

## File: /assets/tray_icon.ico

### Purpose
The tray icon (notification area next to the clock).

### Summary
Static asset; no executable behavior.

### Technical Details
ICO with PNG-encoded frames of 16, 20, 24, 32, 40, 48 and 64 px, rendered from `source/tray_icon.svg` (the
switch without the tile, with a thin darker outline so it reads on light and dark taskbars). Built into the
executable with `include_bytes!`; `ico::pick_frame` selects the frame for the monitor's scale.

### Business Logic
Implements rule T4.

### Dependencies
`src-tauri/src/lib.rs` (`tray_icon`), `src-tauri/src/ico.rs`. Written by `tools/set-icons.ps1`.

## File: /assets/header_logo.png

### Purpose
The logo inside the app window, in the header.

### Summary
Static asset; no executable behavior.

### Technical Details
256×256 PNG of the application icon, displayed at 40 px. `build.rs` copies it to `ui/logo.png` (a generated,
git-ignored file) because the window serves the `ui/` folder.

### Business Logic
None.

### Dependencies
`src-tauri/build.rs`, `ui/index.html` (as `logo.png`). Written by `tools/set-icons.ps1`.

## File: /assets/source/app_icon.svg

### Purpose
Editable source of the built-in application icon.

### Summary
Static asset; no executable behavior.

### Technical Details
SVG, viewBox 100×100: dark gradient tile with a subtle edge, glowing blue switch track with a shine, an
"on" bar, and a white knob with a drop shadow carrying the blue USB mark.

### Business Logic
None.

### Dependencies
Used by `set-icons.bat --reset` (frames of 32 px and up, and the header logo).

## File: /assets/source/app_icon_small.svg

### Purpose
Simplified built-in application icon for small sizes.

### Summary
Static asset; no executable behavior.

### Technical Details
SVG identical to `app_icon.svg` without the USB mark, the "on" bar and the glow, so it stays clean at 16–24 px.

### Business Logic
None.

### Dependencies
Used by `set-icons.bat --reset` (16, 20 and 24 px frames of `app_icon.ico`).

## File: /assets/source/tray_icon.svg

### Purpose
Editable source of the built-in tray icon.

### Summary
Static asset; no executable behavior.

### Technical Details
SVG: the blue switch (pill with white knob) filling the square without the tile, with a thin darker outline.

### Business Logic
None.

### Dependencies
Used by `set-icons.bat --reset`.
