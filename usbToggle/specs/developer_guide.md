# usbToggle — Developer Guide

This guide explains how to set up a development environment, build and run usbToggle, debug it,
extend it, and contribute changes. Paths are relative to `usbToggle/`.

## 1. Setup

### 1.1 Requirements

| Tool | Version | Needed for |
| --- | --- | --- |
| Windows | 10 or 11, x64 | Running the app (device APIs are Windows-only) |
| Rust | stable, 1.77.2 or newer, target `x86_64-pc-windows-msvc` | Back end |
| Visual Studio Build Tools | 2022, "Desktop development with C++" | MSVC linker and Windows SDK for Rust |
| Node.js | 18 or newer (with npm) | Installs and runs the Tauri CLI |
| Microsoft Edge WebView2 Runtime | any current | Rendering the window (preinstalled on Windows 10/11) |
| Inno Setup | 6.3 or newer (optional) | Building the installer |

Install Inno Setup with `winget install JRSoftware.InnoSetup`.

### 1.2 First-time setup

```bat
cd usbToggle
npm install
```

`npm install` installs `@tauri-apps/cli` locally. There is no JavaScript build step and no front-end
dependency.

### 1.3 Project layout

```
usbToggle/
  build.bat                 release build: exe, portable ZIP, installer
  assets/                   app_icon.ico, tray_icon.ico, header_logo.png, source/ (SVG artwork)
  package.json              Tauri CLI
  installer/                Inno Setup script, portable marker
  ui/                       window: index.html, app.js, styles.css, icons.js, logo.svg
  src-tauri/
    Cargo.toml, Cargo.lock, build.rs, tauri.conf.json, capabilities/
    src/                    Rust: lib.rs (core), devices.rs, classify.rs, control.rs, config.rs,
                            autostart.rs, elevation.rs, notify.rs, paths.rs, logger.rs, win.rs, main.rs
  specs/                    this documentation
```

## 2. Dependencies

### 2.1 Rust crates (`src-tauri/Cargo.toml`)

| Crate | Purpose |
| --- | --- |
| `tauri` 2 (`tray-icon`, `image-png`, `image-ico`) | Window, tray, IPC, events; decoding of the tray `.ico` |
| `tauri-plugin-single-instance` 2 | One running instance |
| `windows-sys` 0.61 | Win32 bindings (SetupAPI, CfgMgr32, Properties, Security, Threading, Shell) |
| `serde`, `serde_json` | IPC payloads, `config.json` |
| `chrono` | Timestamps |
| `tauri-build` 2 (build) | Embeds icons and config at compile time |

Versions are pinned in `src-tauri/Cargo.lock`, which is committed.

### 2.2 npm

| Package | Purpose |
| --- | --- |
| `@tauri-apps/cli` 2 | `tauri dev` and `tauri build` |

## 3. Run instructions

### 3.1 Development

```bat
npm run dev
```

This compiles the Rust back end in debug mode and opens the window, serving `ui/` directly.
Changes to Rust files trigger a rebuild and restart. The page blocks F5 and Ctrl+R (they refresh
the device list instead of reloading the page), so restart `npm run dev` to be sure UI changes are
loaded.

To switch devices during development, run the terminal **as administrator**. Otherwise the app
lists devices but shows the *Restart as admin* banner.

### 3.2 Release build

```bat
build.bat
```

Output in `dist\`:

| Output | Description |
| --- | --- |
| `usbToggle-<version>-setup.exe` | Installer (only when Inno Setup is installed) |
| `usbToggle-<version>-portable.zip` | Portable package: `usbToggle.exe` + `portable.txt` |
| `usbToggle-<version>-portable\` | The unzipped portable folder |

The raw executable is `src-tauri\target\release\usbToggle.exe`. The version comes from
`src-tauri\tauri.conf.json`; update it there (and in `Cargo.toml` and `package.json` to keep them
aligned).

### 3.3 Command-line flags

| Flag | Effect |
| --- | --- |
| `--tray` / `--hidden` | Start in the tray without showing the window |
| `--autostart` | Start as Windows would (hidden if *Start in the tray* is on) |
| `--wait-for-pid <pid>` | Wait for a process to exit first (used by *Restart as admin*) |

### 3.4 Portable mode

Create an empty `portable.txt` next to the executable. The app then keeps settings, logs and the
WebView2 profile in `data\` next to the executable.

## 4. Testing

### 4.1 Unit tests

The pure logic (`classify.rs`, `devices::assemble`, `config.rs`) has unit tests:

```bat
cd src-tauri
cargo test
```

Tests cover VID/PID and port parsing, `_PLD` panel parsing, category and name rules, interface
folding, root-hub exclusion, hub linking, built-in detection, panel inheritance, configuration
loading, the v2→v3 migration and removal of empty device entries.

### 4.2 Checking on non-Windows machines

The crate targets Windows. On Linux or macOS you can still type-check the full Windows build:

```bash
rustup target add x86_64-pc-windows-msvc
cd src-tauri
cargo clippy --target x86_64-pc-windows-msvc
```

The pure modules can be tested on any platform by compiling them into a small test crate (they
do not depend on Tauri or Win32).

### 4.3 Manual test checklist

- Plug in and remove a device: the list updates within about a second.
- Turn a device off and on, from the window and from the tray (after pinning it).
- Keyboards and mice are in "Protected" with a greyed switch and a greyed lock; clicking either explains why.
  Protect another device with its lock: it moves to "Protected", the tray item shows "(protected)", and its
  lock is greyed too. Turn off *Protect devices* in Settings: a warning dialog appears; after confirming and
  saving, locks work again and unprotecting a device asks for confirmation.
- A Wi-Fi + Bluetooth combo card's Bluetooth half is listed as a Bluetooth adapter (built-in), not as
  audio, and not named after the Wi-Fi card.
- Run without admin rights: banner, `accessDenied` toast with *Restart as admin*, relaunch works.
- Toggle *Only devices on USB ports*, *Show USB hubs*, *Show disconnected devices*,
  *Hide devices that aren't working* (a device with code 43 disappears and is named in a note).
- Rename, reset the name, pin and unpin; restart the app and check they persist.
- Choose an icon in a device's details (quick row, then *More icons* with search); *Auto* goes back to the
  detected icon. Restart and check the choice persists.
- Settings: toggling a switch changes nothing until *Save*; × / Esc / click outside with changes shows
  *Discard* / *Save*; *Discard* restores the previous theme; Ctrl+S saves.
- Run `set-icons.bat` with a test PNG, then `build.bat`: the exe, installer, taskbar, tray and header logo all
  show the new icon; `set-icons.bat --reset` restores the default. Also try copying your own `.ico` files over
  the ones in `assets/` and rebuilding.
- Enable *Start with Windows* as admin; sign out and in; the app starts in the tray without UAC.
- Close the window (hides to tray), *Quit* from the tray, launch twice (focuses the first window).
- Damage `config.json`: the app starts with defaults, shows a notice and leaves a `.bak.json`.
- Portable ZIP: settings are written to `data\`, nothing to `%APPDATA%\usbToggle`.
- Install, upgrade over a running instance, uninstall (task removed; settings prompt).

## 5. Debugging

### 5.1 Logs

The log is `%LOCALAPPDATA%\usbToggle\logs\usbToggle.log` (portable: `data\logs\`). It records
startup (version, elevation, hidden), each enable/disable with its result, scan failures,
configuration problems, tray menu errors and the fallback to polling. Settings → *Logs* opens the
folder. The file rotates to `usbToggle.old.log` at 1 MB.

### 5.2 Developer tools

In debug builds (`npm run dev`), Tauri enables the WebView2 developer tools. The app blocks the
right-click menu, so open them with **Ctrl+Shift+I**. The front-end state is inside the `app.js` closure; the
quickest way to inspect data is to call commands from the console:

```js
await __TAURI__.core.invoke("get_devices", { force: true });
```

### 5.3 Device data

To compare with what Windows reports:

```powershell
Get-PnpDevice -PresentOnly | Where-Object InstanceId -like 'USB\*'
Get-PnpDeviceProperty -InstanceId '<id>' | Format-Table KeyName, Data
```

Useful keys: `DEVPKEY_Device_BusReportedDeviceDesc`, `DEVPKEY_Device_ContainerId`,
`DEVPKEY_Device_PhysicalDeviceLocation`, `DEVPKEY_Device_ProblemCode`.

### 5.4 Common problems

| Symptom | Cause / fix |
| --- | --- |
| Window never appears in dev | A JavaScript error before `ui_ready`; the window is forced visible after 4 s. Check the developer tools console. |
| Every switch fails with `accessDenied` | The process is not elevated. Use *Restart as admin* or an elevated terminal. |
| `failed` with "Use the 64-bit build" | A 32-bit build is running on 64-bit Windows. Build for x64. |
| Everything is listed as built-in, or nothing is | The firmware's port description; compare `ContainerId` and `Capabilities` with PowerShell. |
| No front/back grouping | The firmware does not provide `_PLD` panel data for its ports. |
| Startup task not created | `set_autostart` was called without elevation (Run value used instead), or `schtasks` failed; see the toast and the log. |
| Two tray icons | A tray icon was also declared in `tauri.conf.json`; the app creates its own in code. |

## 6. Extension guide

### 6.1 Add a device category

1. Add a variant to `Category` in `classify.rs` and its `label()`.
2. Add a detection rule in `categorize()` at the right priority, plus a unit test.
3. Add a label in `CATEGORY` in `ui/app.js`, an icon with the same name in `ui/icons.js`, and a
   `.cat-<name>` color rule in `ui/styles.css`.
4. If turning it off is risky, add it to `RISKY` in `ui/app.js` and a sentence in `confirmDisable()`.

### 6.2 Add a vendor

Add the VID (four upper-case hex digits) and company name to `vendor_name()` in `classify.rs`.

### 6.3 Add a setting

1. Add the field to `Settings` in `config.rs` with its default in `Default`. Because of
   `#[serde(default)]`, existing files load without migration. If existing users must get the new
   default, bump `SCHEMA_VERSION` and add a migration step in `Store::load()` with a test.
2. Add the key and default to `DEFAULT_SETTINGS` in `ui/app.js`.
3. Add a row with `data-setting="<camelCaseName>"` in the Settings dialog in `ui/index.html`;
   `bind()` and `syncSettingsDialog()` handle it automatically.
4. Use it in `render()` (front end) or read it from `Core.config` (back end).

### 6.4 Add an IPC command

1. Write a `#[tauri::command]` function in `lib.rs`. If it touches Windows or the disk, make it
   `async` and wrap the work in `blocking(...)`.
2. Register it in `tauri::generate_handler![...]`.
3. Call it from `ui/app.js` with `invoke("name", { camelCaseArgs })`.
4. Document it in `specs/api_reference.md`.

### 6.5 Add a back-end event

Emit with `app.emit("name", payload)` (payload `Serialize`), listen in `init()` in `ui/app.js` with
`listen("name", handler)`, and document it.

### 6.6 Change the application icons

The icons are three files in `assets/`, named after where they show:

| File | Where it shows |
| --- | --- |
| `app_icon.ico` | Window, taskbar, Start menu, the `.exe`, the installer |
| `tray_icon.ico` | Tray icon |
| `header_logo.png` | Logo in the window header |

Replace them by hand (keep the names), or run `set-icons.bat` in the `usbToggle` folder:

```bat
set-icons.bat my-icon.png          one image for everything (or drag it onto the file)
set-icons.bat app.png tray.png     a separate image for the tray
set-icons.bat --reset              back to the built-in icons
```

Images must be square PNG or SVG (512×512 or larger, transparent background). By hand, `app_icon.ico` should
contain 16, 24, 32, 48 and 256 px frames and `tray_icon.ico` at least 16, 24 and 32 px (the tray picks the frame
that fits the screen scale). Rebuild with `build.bat` afterwards. `build.rs` copies `header_logo.png` to
`ui/logo.png`, a generated file that git ignores. If Windows still shows the old icon on the taskbar or desktop,
unpin and re-pin the app (Windows caches icons). The built-in artwork is in `assets/source/`.

### 6.7 Add or change library icons

Edit `tools/icon-library.txt` (`## Label | color` starts a group, followed by Lucide icon names from
https://lucide.dev/icons) and run:

```bat
npm install
npm run icons:library
```

This rewrites `ui/icon-library.js`. Names that do not exist in the installed Lucide version are reported and
skipped. Keep the Lucide license notice in the generated header.

## 7. Style rules

### 7.1 Rust

- Format with `cargo fmt` defaults; keep `cargo clippy --target x86_64-pc-windows-msvc` free of warnings.
- Keep Win32 calls inside `#[cfg(windows)] mod imp` blocks and provide a non-Windows stub for public
  functions, so the pure modules stay testable.
- Put decision logic in pure functions (`classify.rs`, `devices::assemble`) and test them.
- Wrap handles in types that release them on `Drop` (see `DevInfoSet`).
- Never hold a `Core` mutex while calling a Tauri API that may dispatch to the main thread (menu
  creation, `set_menu`, window operations).
- Never block the main thread: use `async` commands with `blocking()`, and move work out of menu
  and window event handlers with `std::thread::spawn`.
- Errors shown to users are full sentences in plain language; log the technical detail with
  `log_error!`.
- Comments explain *why*, not what.

### 7.2 Front end

- Plain JavaScript in the existing IIFE; no frameworks or build tools.
- Create elements with `h()`; update rows in place (`updateRow`) and order them with `reconcile()`
  so focus and scroll position survive re-renders.
- Treat back-end data as the source of truth; `state.pending` only overlays optimistic switch states.
- Wrap every `localStorage` access in `try/catch` (through the `store` helper).
- Use CSS custom properties for colors and define both light and dark values.
- Keep controls keyboard-accessible: real `<button>` elements, `role="switch"` with `aria-checked`,
  visible focus rings.
- UI text: sentence case, short, and specific ("Turn off", not "Disable device").

## 8. Contribution guide

1. Work on a feature branch; keep commits focused, with a short imperative subject line and a body
   that explains why.
2. Before committing:
   - `cargo test` (from `src-tauri/`),
   - `cargo clippy --target x86_64-pc-windows-msvc` with no warnings,
   - run the app and check the affected part of the manual checklist (section 4.3).
3. Update the user README (`README.md`) for user-visible changes and the files in `specs/` for
   changes to architecture, rules, commands, events or data models.
4. Do not commit build output (`dist/`, `node_modules/`, `src-tauri/target/`) or `package-lock.json`.
   Do commit `src-tauri/Cargo.lock`.
5. Bump the version in `tauri.conf.json`, `Cargo.toml` and `package.json` together for releases.
