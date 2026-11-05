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
- Shows only devices plugged into real USB ports by default. Parts of the PC
  that use USB internally (built-in webcam, Bluetooth, fingerprint reader,
  motherboard hub chips) are hidden; turn off **Only devices on USB ports** in
  Settings to see them.
- Groups connected devices by where the port is: **Front panel**, **Back
  panel** (or left/right side on a laptop), when the motherboard firmware
  provides it; anything else goes under "Other ports". Devices behind a hub
  follow the hub's port. Search for "front" or "back" to filter. Some boards report every port as "back" or don't say.
- The list updates by itself when you plug in or remove something.
- Optionally, devices Windows remembers but that aren't plugged in (Settings),
  so you can turn one off before plugging it in. Disconnected devices that are
  off are always listed.
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

Requires [Rust](https://rustup.rs) and [Node.js](https://nodejs.org). For the
installer, also [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php)
(`winget install JRSoftware.InnoSetup`).

```bat
build.bat
```

Output in `dist\`:

| File                               | What                                                  |
| ---------------------------------- | ----------------------------------------------------- |
| `usbToggle-<version>-setup.exe`    | Installer (Program Files, Start menu, uninstaller)    |
| `usbToggle-<version>-portable.zip` | Portable version: unzip anywhere and run, no install   |

Without Inno Setup, the build still makes the portable version and says how to
get the installer. For development: `npm install` then `npm run dev`.

The app needs the Microsoft Edge WebView2 Runtime, which is included with
Windows 10 and 11. The installer checks for it.

## Files

|             | Installed                         | Portable (`portable.txt` next to the exe) |
| ----------- | --------------------------------- | ----------------------------------------- |
| Settings    | `%APPDATA%\usbToggle\config.json` | `data\config.json`                        |
| Log         | `%LOCALAPPDATA%\usbToggle\logs\`  | `data\logs\`                              |
| WebView data| `%LOCALAPPDATA%\com.usbtoggle.app` | `data\webview\`                           |

Command-line flags: `--tray` starts hidden in the tray.

## Layout

- `ui/` — the window (plain HTML/CSS/JS, no build step).
- `installer/` — Inno Setup script and the portable marker file.
- `src-tauri/src/devices.rs` — finds USB devices (SetupAPI / CfgMgr32).
- `src-tauri/src/classify.rs` — names and device types.
- `src-tauri/src/control.rs` — turns devices on and off.
- `src-tauri/src/notify.rs` — plug/unplug notifications.
- `src-tauri/src/lib.rs` — tray, window, and commands for the UI.
