# usbToggle

Windows tray app that turns USB devices on and off — webcams, microphones,
keyboards, flash drives, controllers, Bluetooth adapters, hubs.

Turning a device off does what Device Manager's **Disable device** does: Windows
unloads its driver and the device stops working. It stays off after unplugging or
restarting, until you turn it back on. The port itself can stay powered, so
chargers and LEDs may keep working.

## Features

- Every USB device with a readable name and type (from the name the device
  reports and the drivers it loads), not "USB Composite Device".
- One switch per device. Changes go through the Windows class installer directly
  (no PowerShell), so they take about a second.
- The list updates by itself when you plug in or remove something.
- Devices Windows remembers but that aren't plugged in. You can turn one off
  before plugging it in.
- Rename devices, and pin the ones you use most: they stay at the top and appear
  in the tray icon menu, where you can switch them without opening the window.
- Asks before turning off keyboards, mice, hubs and Bluetooth adapters.
- Search (`Ctrl+F`), On/Off filter, refresh (`F5`), light and dark themes.
- Closing the window keeps it in the tray. One instance at a time.
- Optional **Start with Windows**. If you turn it on while running as
  administrator, it starts with admin rights and no UAC prompt (a logon task).

## Administrator rights

Windows only lets administrators enable or disable devices. Without admin rights
the app still lists everything and shows a **Restart as admin** button.

## Build

Requires [Rust](https://rustup.rs), [Node.js](https://nodejs.org) and the
WebView2 runtime (included with Windows 10 and 11).

```bat
build.bat
```

This produces `usbToggle.exe` in this folder, and MSI/NSIS installers in
`src-tauri\target\release\bundle\`. For development: `npm install` then
`npm run dev`.

## Files

| What        | Where                                      |
| ----------- | ------------------------------------------ |
| Settings    | `%APPDATA%\usbToggle\config.json`          |
| Log         | `%LOCALAPPDATA%\usbToggle\logs\`           |

Command-line flags: `--tray` starts hidden in the tray.

## Layout

- `ui/` — the window (plain HTML/CSS/JS, no build step).
- `src-tauri/src/devices.rs` — finds USB devices (SetupAPI / CfgMgr32).
- `src-tauri/src/classify.rs` — names and device types.
- `src-tauri/src/control.rs` — turns devices on and off.
- `src-tauri/src/notify.rs` — plug/unplug notifications.
- `src-tauri/src/lib.rs` — tray, window, and commands for the UI.
