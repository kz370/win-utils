# USB Toggle

Turn USB devices on and off from your Windows tray: webcams, microphones, keyboards, flash
drives, game controllers, Bluetooth adapters, hubs and more.

Turning a device **off** does what *Disable device* does in Device Manager: Windows unloads the
device's driver and the device stops working. It stays off after you unplug it or restart the PC,
until you turn it back on. The USB port itself may keep supplying power, so chargers and lights
can keep working.

- [Install](#install)
- [First start](#first-start)
- [Using the list](#using-the-list)
- [Turning devices on and off](#turning-devices-on-and-off)
- [Protect devices](#protect-devices)
- [Rename, pin and choose an icon](#rename-pin-and-choose-an-icon)
- [The tray icon](#the-tray-icon)
- [Settings](#settings)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Change the app icon](#change-the-app-icon)
- [Troubleshooting](#troubleshooting)
- [Where things are stored](#where-things-are-stored)
- [Build it yourself](#build-it-yourself)

## Install

You get two files from the build (see [Build it yourself](#build-it-yourself)):

| File | What it is |
| --- | --- |
| `usbToggle-<version>-setup.exe` | Installer. Adds a Start menu entry (and an optional desktop shortcut) and an uninstaller. |
| `usbToggle-<version>-portable.zip` | Portable version. Unzip anywhere and run `usbToggle.exe`. Nothing is installed, and all settings stay in a `data` folder next to it, so you can keep it on a USB stick. |

Requirements: Windows 10 or 11 (64-bit) and the Microsoft Edge **WebView2 Runtime**, which
Windows 10 and 11 already include. The installer warns you if it is missing.

To uninstall, use *Settings → Apps → Installed apps* (or the uninstaller in the Start menu). It
asks whether to delete your saved settings too. For the portable version, delete the folder.

## First start

Windows only lets **administrators** turn devices on or off. You can open USB Toggle without
admin rights and look around, but switches won't work. In that case a banner appears at the top:

> **Administrator rights needed** — [Restart as admin]

Click **Restart as admin**, approve the Windows prompt, and the app reopens with full control.
You can also right-click `usbToggle.exe` → *Run as administrator*.

To avoid the prompt every time, turn on **Start with Windows** in [Settings](#settings) while
the app is running as administrator. It then starts at sign-in with admin rights and no prompt.

## Using the list

The top of the window shows a summary such as **8 connected · 1 off**. Next to it are the
refresh button (also **F5**) and the settings button.

- **Search** (click the box, or press **Ctrl+F** or **/**) matches names, types, makers, ports,
  "front" or "back", and device IDs. Several words must all match.
- **All / On / Off** filters the list.

The list is grouped by where each device is plugged in:

| Section | What is in it |
| --- | --- |
| **Pinned** | Devices you pinned, always at the top. |
| **Front panel**, **Back panel**, **Left side**, **Right side**, **Top**, **Bottom** | Connected devices, by the side of the case their port is on. Devices plugged into a hub are listed where the hub is plugged in. |
| **Other ports** | Devices whose port location your PC doesn't report. If it reports none at all, this section is simply called **Connected**. |
| **Protected** | Keyboards, mice and anything else you protected. See [Protect devices](#protect-devices). |
| **Built into this PC** | Only shown if you turn off *Only devices on USB ports* in Settings. |
| **USB hubs** | Only shown if you turn on *Show USB hubs*. |
| **Not connected** | Only shown if you turn on *Show disconnected devices*. Devices that are **off** are always listed here. |

> Front and back come from your motherboard's firmware. Some boards report every port as "back"
> or say nothing, and then there is nothing the app can do about it.

Each row shows an icon, the name and a line such as `Storage · Front panel · Port 3`:

- **Off** (in bold) means you turned the device off.
- **Problem (code 43)** in red means Windows says the device isn't working.
- *Not connected · seen 3 days ago* means Windows remembers the device but it isn't plugged in.
- *via Terminus Technology USB2.0 HUB* means it is plugged into that hub.

**Click a row** for details: status, type, maker, hardware ID, how it is connected, when it was
last seen, what it contains, and the Windows instance ID (with a copy button). If Windows reports
a problem, the panel explains it in plain words.

When you plug in or remove a device, the list updates by itself within a second.

### What is hidden by default

To keep the list short, USB Toggle hides things you usually don't want to touch:

- **Devices built into the PC**, like a laptop webcam, a Bluetooth adapter or a fingerprint reader.
  They are on the motherboard, not in a port. (A Wi-Fi + Bluetooth card is one example: Windows
  lists its Bluetooth half as a USB device.)
- **Devices that aren't working**, such as ones showing code 43.
- **USB hubs** and **disconnected devices**.

A note at the end of the list tells you what is hidden (for example *3 built-in devices are
hidden: …*) with a **Show them** button. Each of these has its own switch in [Settings](#settings).

## Turning devices on and off

Click the **switch** on a row. It shows a spinner while Windows works, usually about a second,
and then a message appears with an **Undo** button.

- **Blue** is on. **Grey** is off.
- Turning off a **keyboard, mouse, hub or Bluetooth adapter** asks first, because it can cut off the
  device you are using to control the PC. Tick *Don't ask again* if you don't want to be asked.
- Turning off a **hub** turns off everything plugged into it.
- You can turn off a device that is **not plugged in**. It will stay off when you plug it in.
- You can turn a device back on at any time, from the window or the tray.

If Windows needs a restart to finish a change, the app says so.

## Protect devices

Switching off your only keyboard or mouse by accident is annoying, so USB Toggle **protects
keyboards and mice automatically**. They stay in the list, but their switch is greyed out and they
sit in a **Protected** section with a small padlock next to the name. Clicking the switch explains
why nothing happened.

- **Protect any device:** hover a row and click the **lock** button (or open the row and use it).
- **Unprotecting is deliberately hard.** While **Protect devices** is on in Settings, a protected device
  **can't be unprotected**: its lock is greyed out. You can protect more devices, but not remove protection.
- **To unprotect devices,** turn off **Protect devices** in Settings. You are asked to confirm, with a
  warning. After that, the lock on each device works again, and unprotecting a device asks for another
  confirmation.

Protection only blocks turning a device **off**. You can always turn a protected device **on**.
It applies in the tray menu too, where protected devices show *(protected)*.

## Rename, pin and choose an icon

Open a row's details to:

- **Rename it.** Type a name and click **Save name** (or press Enter). **Use default** goes back to
  the automatic name.
- **Choose an icon.** **Auto** uses the icon for the detected type. The quick row has common icons;
  **More icons** opens a searchable library of 239 icons (type "guitar", "light", "fan", …). Press
  Enter in the search box to pick the first match. **Upload your own image** uses a picture from your PC
  (PNG, JPG, SVG, GIF, WebP, BMP or ICO): up to **2 MB**, between **16 and 4096 px**, and not a thin strip
  (the long side at most 4× the short side). It is shrunk to a small icon and saved with your other settings.

On the row itself (hover it), the **pin** button pins the device: it moves to the top and appears in
the tray menu.

## The tray icon

USB Toggle keeps running in the tray (by default, closing the window just hides it).

| Action | Result |
| --- | --- |
| **Left-click** the tray icon | Shows the window, or hides it if it is open. |
| **Right-click** the tray icon | Menu with your **pinned devices** (a tick means on; click to switch), **Open USB Toggle**, **Refresh devices** and **Quit**. |
| Hover | The tooltip says how many connected devices are off. |

Pin the devices you switch often (your webcam, a microphone, a USB drive) and you never need to open
the window. If a switch from the tray fails, the window opens and shows why.

To fully close the app, use **Quit** in the tray menu or in Settings. Starting USB Toggle a second
time just brings the first window to the front.

## Settings

Click the gear. **Nothing changes until you press Save** (or **Ctrl+S**); the dialog shows
*Unsaved changes* while you have edits. If you close it with unsaved changes (the × button,
**Cancel**, **Esc** or clicking outside), you are asked whether to **Save** or **Discard**. The theme
previews as you click it and goes back if you discard.

| Setting | What it does | Default |
| --- | --- | --- |
| **Theme** | System, Light or Dark. | System |
| **Only devices on USB ports** | Hides devices built into the PC. | On |
| **Show disconnected devices** | Lists remembered devices that aren't plugged in. | Off |
| **Show USB hubs** | Lists hubs. Turning a hub off turns off everything on it. | Off |
| **Hide devices that aren't working** | Hides devices Windows reports a problem with. | On |
| **Protect devices** | Keyboards and mice are protected automatically. While it is on, protected devices can't be turned off or unprotected. Turning it off asks for confirmation. | On |
| **Confirm risky changes** | Asks before turning off keyboards, mice, hubs and Bluetooth adapters. | On |
| **Keep running in the tray** | Closing the window hides it instead of quitting. | On |
| **Start with Windows** | Starts the app when you sign in. As administrator it starts with admin rights and no prompt. | Off |
| **Start in the tray** | When started with Windows, doesn't open the window. | On |

The **About** section has buttons to open the settings folder and the logs, and to quit the app.

## Keyboard shortcuts

| Keys | Action |
| --- | --- |
| **Ctrl+F** or **/** | Search |
| **F5** or **Ctrl+R** | Refresh the list |
| **Esc** | Clear the search, or close open details |
| **Ctrl+S** | Save (while Settings is open) |
| **Tab**, **Space** | Move to a switch and flip it |

You can start `usbToggle.exe --tray` to open it hidden in the tray.

## Change the app icon

All icons are in the **`assets`** folder, named after where they show:

| File | Where it shows |
| --- | --- |
| `assets\app_icon.ico` | Window, **taskbar**, Start menu, the `.exe` file, the installer |
| `assets\tray_icon.ico` | The tray icon next to the clock |
| `assets\header_logo.png` | The logo at the top inside the app window |

Replace them with your own files (**keep the names**), or let `set-icons.bat` make all three from one
square PNG or SVG (512×512 or larger):

```bat
set-icons.bat my-icon.png          one image for everything (you can also drag it onto the file)
set-icons.bat app.png tray.png     a separate image for the tray
set-icons.bat --reset              back to the default icons
```

Then run `build.bat`. If Windows still shows the old icon, unpin and re-pin the app: Windows caches
icons. More in `assets\README.md`.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| Switches don't work, or "needs administrator rights" | Click **Restart as admin** or run the app as administrator. |
| "That image is too small / too large …" when uploading an icon | Use an image of 16–4096 px per side, up to 2 MB, closer to square than 4:1. |
| A device isn't in the list | It may be hidden: look for the note at the end of the list, or check *Only devices on USB ports*, *Show USB hubs* and *Hide devices that aren't working* in Settings. Searching also shows hidden hubs and disconnected devices (but not built-in or not-working ones: use *Show them*). Try **Refresh**. |
| I turned off my keyboard or mouse | Use the other one (or the touch screen) to turn it back on. Keyboards and mice are protected by default so this shouldn't happen. If neither works, restart the PC and use Device Manager (*Enable device*). |
| A device shows **Problem (code 43)** | Windows stopped the device because it reported a problem. Unplug it and plug it in again, try another port, or check the cable. |
| "Windows doesn't allow this device to be turned off" | Windows protects some devices. Nothing to do. |
| "Restart needed" | Windows can't apply the change until you restart. |
| Everything is under "Back panel" (or nothing is grouped) | Your motherboard's firmware describes its ports that way. It can't be changed from here. |
| A hub appears twice | A USB 3 hub contains a USB 2 hub and a USB 3 hub, so Windows lists two. |
| I don't see my empty ports | USB Toggle lists devices, not ports. Windows can switch off a device or a whole hub, but not an empty port. |
| The device is off but the light still shines or the phone still charges | Disabling a device unloads its driver but doesn't cut power to the port. |
| Settings file is damaged | The app starts with defaults, shows a notice and keeps a copy of the damaged file named `config.<date>.bak.json`. |

If something else goes wrong, open Settings → **Logs**. The log records every on and off request
and any error.

## Where things are stored

| | Installed | Portable |
| --- | --- | --- |
| Settings (names, pins, locks, icons, options) | `%APPDATA%\usbToggle\config.json` | `data\config.json` next to the exe |
| Log | `%LOCALAPPDATA%\usbToggle\logs\` | `data\logs\` |
| Browser data (WebView2) | `%LOCALAPPDATA%\com.usbtoggle.app` | `data\webview\` |

A file named `portable.txt` next to `usbToggle.exe` is what makes the app portable. Delete it to
use the normal folders instead.

## Build it yourself

You need [Rust](https://rustup.rs) and [Node.js](https://nodejs.org). For the installer, also
[Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`).

```bat
build.bat
```

The results are in `dist\`: the installer (when Inno Setup is installed), the portable ZIP and the
unzipped portable folder. Without Inno Setup you still get the portable version.

For how the app works, how to run it for development and how to extend it, see the
documentation in [`specs/`](specs/): `developer_guide.md`, `architecture_overview.md`,
`api_reference.md` and `full_documentation.md`.

Command-line flags: `--tray` (or `--hidden`) starts hidden in the tray.
