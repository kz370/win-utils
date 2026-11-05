# usbToggle — API Reference

usbToggle has no network API. Its API is the **Tauri IPC interface** between the window
(`ui/app.js`, running in WebView2) and the Rust back end (`src-tauri/src/lib.rs`):

- **Commands** — request/response calls from the window to the back end, made with
  `window.__TAURI__.core.invoke(name, args)`. They return a Promise.
- **Events** — messages pushed from the back end to the window, received with
  `window.__TAURI__.event.listen(name, handler)`.

All payloads are JSON. Field names are **camelCase** on the wire (Rust structs use
`#[serde(rename_all = "camelCase")]`). Argument names in `invoke` are also camelCase.

Commands are only reachable from the app's own window (`main`); there is no external entry
point. Permissions: the window has `core:default` (`src-tauri/capabilities/default.json`);
application commands need no extra permission.

Error convention: a command that can fail rejects its Promise with a **string** message. The
exception is `set_device_enabled`, which always resolves and reports failures in its result.

---

## 1. Shared types

### 1.1 `Device`

One physical USB device. Produced by `devices::assemble()` and `lib::apply_config()`.

| Field | Type | Description |
| --- | --- | --- |
| `id` | string | Windows device instance ID, e.g. `USB\VID_046D&PID_C52B\6&3&0&1`. The device's identity. |
| `defaultName` | string | Automatic display name. |
| `name` | string | Name to display: the custom name if set, else `defaultName`. |
| `customName` | string \| null | User-chosen name. |
| `description` | string | Windows device description (e.g. "USB Composite Device"). |
| `vendor` | string \| null | Company name from the VID table. |
| `vid`, `pid` | string \| null | 4-digit hexadecimal vendor and product IDs, upper case. |
| `category` | string | One of `keyboard`, `mouse`, `input`, `gamepad`, `audio`, `camera`, `storage`, `phone`, `bluetooth`, `network`, `printer`, `serial`, `security`, `hub`, `other`. |
| `present` | boolean | Connected right now. |
| `enabled` | boolean | Not disabled in Windows. |
| `problem` | number | Device Manager problem code for a connected device; 0 = working. 22 means disabled. |
| `isHub` | boolean | The device is a USB hub. |
| `hubId` | string \| null | Instance ID of the external hub the device is plugged into. |
| `hubName` | string \| null | Display name of that hub. |
| `port` | number \| null | Port number on the parent hub. |
| `builtIn` | boolean | Part of the PC (internal port) rather than plugged into a real port. |
| `panel` | string \| null | `front`, `back`, `left`, `right`, `top` or `bottom`, when the firmware reports it. |
| `lastSeen` | number \| null | Last arrival (connected) or removal (disconnected), Unix time in ms. |
| `functions` | string[] | Names of the functions under the device (e.g. "HID Keyboard Device"). Empty for hubs. |
| `pinned` | boolean | Pinned by the user (top of the list and tray menu). |
| `locked` | boolean | Protected: cannot be turned off (can still be turned on). |
| `autoLocked` | boolean | Protected by the automatic *Protect devices* rule (keyboards and mice) rather than by the user. |
| `customIcon` | string \| null | Icon chosen by the user (a built-in icon name such as `mic`, or `l-<name>` from the icon library). `null` means the icon of the detected `category`. |

### 1.2 `Snapshot`

| Field | Type | Description |
| --- | --- | --- |
| `devices` | `Device[]` | Sorted by `name` (case-insensitive), then `id`. |
| `busy` | string[] | IDs with an enable/disable operation in progress, sorted. |
| `scannedAt` | number | Time of the last scan, Unix ms (0 before the first scan). |
| `error` | string \| null | Message of the last scan failure, or null. |

### 1.3 `Settings`

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `theme` | `"system"` \| `"light"` \| `"dark"` | `"system"` | Window theme. |
| `closeToTray` | boolean | true | Closing the window hides it instead of exiting. |
| `startMinimized` | boolean | true | When started with Windows, stay in the tray. |
| `showDisconnected` | boolean | false | List remembered devices that are not plugged in (devices that are off are always listed). |
| `showHubs` | boolean | false | List USB hubs. |
| `portsOnly` | boolean | true | Hide devices built into the PC. |
| `confirmRisky` | boolean | true | Ask before turning off keyboards, mice, hubs and Bluetooth adapters. |
| `hideProblems` | boolean | true | Hide connected devices with a Windows problem code (other than 22, disabled). |
| `protectInput` | boolean | true | "Protect devices": protects keyboards, mice and keyboard-and-mouse devices automatically, and while on, protected devices cannot be unprotected. |

### 1.4 `AutostartState`

| Field | Type | Description |
| --- | --- | --- |
| `enabled` | boolean | The app starts with Windows (task or Run value present). |
| `elevated` | boolean | It starts through the elevated logon task (no UAC prompt). |

### 1.5 `AppInfo`

| Field | Type | Description |
| --- | --- | --- |
| `version` | string | App version (from `tauri.conf.json`). |
| `elevated` | boolean | The process runs as administrator. |
| `settings` | `Settings` | Current settings. |
| `autostart` | `AutostartState` | Current startup registration. |
| `configPath` | string | Full path of `config.json`. |
| `logDir` | string | Full path of the log folder. |
| `portable` | boolean | Running as the portable version. |
| `configError` | string \| null | Message from loading the configuration (damaged file, newer schema, unreadable file). |

### 1.6 `ToggleResult`

| Field | Type | Description |
| --- | --- | --- |
| `ok` | boolean | The operation succeeded. |
| `code` | string \| null | Error code when `ok` is false (see 1.7). |
| `message` | string \| null | Readable error, or the restart notice when `needsRestart` is true. |
| `needsRestart` | boolean | Windows needs a restart to finish, or the device did not reach the requested state. |
| `snapshot` | `Snapshot` | Device list after the operation. |

### 1.7 Error codes of `set_device_enabled`

| Code | Meaning | Typical message |
| --- | --- | --- |
| `locked` | The device is protected and the request was to turn it off (checked first; Windows is not called) | "This device is protected, so it can't be turned off. Unprotect it first." |
| `accessDenied` | Not running as administrator (checked before calling Windows), or Windows denied access | "Turning devices on or off needs administrator rights. Restart USB Toggle as administrator." |
| `notDisableable` | Windows does not allow this device to be disabled | "Windows doesn't allow this device to be turned off." |
| `missing` | The instance no longer exists | "The device is no longer known to Windows. Refresh and try again." |
| `failed` | Any other failure (including a 32-bit build on 64-bit Windows) | "Couldn't turn off the device: …" |

---

## 2. Commands

### 2.1 `get_info`

Returns general information about the app.

| | |
| --- | --- |
| Arguments | none |
| Returns | `AppInfo` |
| Errors | Only if the background task fails to run (string). |
| Notes | Runs `schtasks` and `reg` to read the autostart state, so it can take a few hundred milliseconds. |

```js
const info = await invoke("get_info");
// { version: "1.1.0", elevated: true, settings: {...}, autostart: { enabled: false, elevated: false },
//   configPath: "C:\\Users\\me\\AppData\\Roaming\\usbToggle\\config.json",
//   logDir: "C:\\Users\\me\\AppData\\Local\\usbToggle\\logs", portable: false, configError: null }
```

### 2.2 `get_devices`

Returns the device list, scanning when needed.

| Argument | Type | Description |
| --- | --- | --- |
| `force` | boolean | `true`: always rescan. `false`: return the cached list, scanning only if no scan has completed yet. |

| | |
| --- | --- |
| Returns | `Snapshot` |
| Side effects | A scan also emits `devices` and rebuilds the tray menu. |
| Errors | Scan failures do not reject; they are reported in `Snapshot.error`. |

```js
const snap = await invoke("get_devices", { force: true });
snap.devices.filter((d) => d.present && !d.enabled); // connected devices that are off
```

### 2.3 `set_device_enabled`

Turns a device on or off.

| Argument | Type | Description |
| --- | --- | --- |
| `id` | string | Device instance ID (`Device.id`). |
| `enabled` | boolean | `true` to turn on, `false` to turn off. |

| | |
| --- | --- |
| Returns | `ToggleResult` (always resolves) |
| Side effects | Emits `busy` (start and end), rescans (emits `devices`, rebuilds tray), writes a log line. |
| Behavior | Operations run one at a time. Connected devices are switched immediately through the class installer; disconnected devices get their persistent disable flag changed and switch when plugged in. |
| Confirmation | None on the back end. The window asks before turning off risky devices (see business rule S8). |

```js
const r = await invoke("set_device_enabled", { id: "USB\\VID_0781&PID_5567\\4C53", enabled: false });
if (!r.ok && r.code === "accessDenied") { /* offer "Restart as admin" */ }
// success: { ok: true, code: null, message: null, needsRestart: false, snapshot: {...} }
```

### 2.4 `rename_device`

Sets or clears a device's custom name.

| Argument | Type | Description |
| --- | --- | --- |
| `id` | string | Device instance ID. |
| `name` | string \| null | New name. `null`, empty or blank clears the custom name. Trimmed before saving. |

| | |
| --- | --- |
| Returns | `Snapshot` (names re-applied, no hardware rescan) |
| Side effects | Saves `config.json`; emits `devices`; rebuilds the tray. |
| Errors | Rejects with the save error message (e.g. "Couldn't save settings: …"). |

```js
await invoke("rename_device", { id, name: "Desk webcam" });
await invoke("rename_device", { id, name: null }); // back to the automatic name
```

### 2.5 `set_pinned`

Pins or unpins a device.

| Argument | Type | Description |
| --- | --- | --- |
| `id` | string | Device instance ID. |
| `pinned` | boolean | New pin state. |

| | |
| --- | --- |
| Returns | `Snapshot` |
| Side effects | Saves `config.json`; emits `devices`; rebuilds the tray menu (pinned devices appear in it). |
| Errors | Rejects with the save error message. |

### 2.6 `set_locked`

Protects or unprotects a device.

| Argument | Type | Description |
| --- | --- | --- |
| `id` | string | Device instance ID. |
| `locked` | boolean | `true` to protect, `false` to unprotect. |

| | |
| --- | --- |
| Returns | `Snapshot` (state re-applied, no hardware rescan) |
| Behavior | `locked: true` stores the user's protection (nothing is stored for a device the automatic rule already protects). `locked: false` clears it, but is **refused** while `protectInput` is on and the device is protected. |
| Side effects | Saves `config.json`; emits `devices`; rebuilds the tray menu. |
| Errors | Rejects with `Protection is on. Turn off "Protect devices" in Settings to unprotect devices.` when unprotecting while protection is on, or with the save error message. |

```js
await invoke("set_locked", { id, locked: true }); // can't be turned off any more
```

### 2.7 `set_icon`

Sets or clears a device's custom icon.

| Argument | Type | Description |
| --- | --- | --- |
| `id` | string | Device instance ID. |
| `icon` | string \| null | Icon name, or `null` (or empty) to use the automatic icon. Must match `[a-z0-9-]{1,32}`. |

| | |
| --- | --- |
| Returns | `Snapshot` (no hardware rescan) |
| Side effects | Saves `config.json`; emits `devices`. |
| Errors | Rejects with `Unknown icon "<name>".` for an invalid name, or with the save error message. The back end checks only the name format; the window falls back to the automatic icon for names it does not know. |

```js
await invoke("set_icon", { id, icon: "l-guitar" });
await invoke("set_icon", { id, icon: null }); // back to the detected type's icon
```

### 2.8 `save_settings`

Replaces all settings.

| Argument | Type | Description |
| --- | --- | --- |
| `settings` | `Settings` | Complete settings object. Missing fields take defaults. |

| | |
| --- | --- |
| Returns | `Settings` (as saved) |
| Side effects | Saves `config.json`; applies the theme to the native window frame; re-applies device state (protection) and emits `devices`. Display settings are applied by the window itself. |
| Called when | The user presses *Save* (or Ctrl+S) in Settings, or uses an action outside the dialog that saves immediately ("Show them" notes, "Don't ask again"). Toggling a switch inside Settings does **not** call it. |
| Errors | Rejects with the save error message. |

```js
await invoke("save_settings", { settings: { ...current, showHubs: true } });
```

### 2.9 `set_autostart`

Turns "Start with Windows" on or off.

| Argument | Type | Description |
| --- | --- | --- |
| `enabled` | boolean | Desired state. |

| | |
| --- | --- |
| Returns | `AutostartState` after the change |
| Behavior | Called by the window when *Save* is pressed in Settings and *Start with Windows* changed. Elevated process: creates the logon task `usbToggle` (highest privileges) and removes the Run value. Non-elevated: writes the Run value. Off: removes both. |
| Errors | Rejects with a message, e.g. "Couldn't create the startup task: …" or "Couldn't remove the startup task. Run USB Toggle as administrator to change this." |

### 2.10 `restart_as_admin`

Starts an elevated copy of the app and exits this one.

| | |
| --- | --- |
| Arguments | none |
| Returns | nothing (the process exits on success) |
| Behavior | Shows the UAC prompt. The new process receives `--wait-for-pid <pid>` (and `--tray` when the window was hidden). |
| Errors | Rejects with "Administrator permission was not granted." when the prompt is declined or fails. |
| Notes | Runs on the main thread; the window is unresponsive while the UAC prompt is open. |

### 2.11 `ui_ready`

Tells the back end that the page has painted its first frame.

| | |
| --- | --- |
| Arguments | none |
| Returns | nothing |
| Behavior | The first call shows the window unless the app was started hidden. Later calls do nothing. |

### 2.12 `open_folder`

Opens a data folder in Explorer.

| Argument | Type | Description |
| --- | --- | --- |
| `which` | `"config"` \| `"logs"` | Folder to open (created first if missing). |

| | |
| --- | --- |
| Returns | nothing |
| Errors | "Unknown folder" for other values; the OS error if Explorer cannot be started. |

### 2.13 `quit`

Exits the application (window and tray).

| | |
| --- | --- |
| Arguments | none |
| Returns | nothing |

---

## 3. Events

### 3.1 `devices`

| | |
| --- | --- |
| Payload | `Snapshot` |
| When | After every scan (startup, refresh, device change, after a switch), after a rename, pin, unpin, protect, unprotect or icon change, and after settings are saved. |
| Window behavior | Replaces the device list and re-renders. |

### 3.2 `busy`

| | |
| --- | --- |
| Payload | string[] — IDs with an operation in progress (not sorted) |
| When | When an enable/disable operation starts and when it ends, including operations started from the tray. |
| Window behavior | Shows a spinner on those switches and "Working…" in their rows. |

### 3.3 `toast`

| | |
| --- | --- |
| Payload | `{ kind: "error", message: string \| null, code: string \| null }` |
| When | A switch started from the tray menu failed. The window is shown first. |
| Window behavior | Shows an error toast; adds *Restart as admin* when `code` is `accessDenied` and the app is not elevated. |

---

## 4. Command-line interface

`usbToggle.exe` accepts these arguments:

| Argument | Effect |
| --- | --- |
| `--tray`, `--hidden` | Start hidden in the tray. |
| `--autostart` | Marks a start by Windows; starts hidden when *Start in the tray* is on. Passed by the logon task and the Run value. |
| `--wait-for-pid <pid>` | Wait up to 5 seconds for process `<pid>` to exit before starting. Used by *Restart as admin*. |

Launching the app while it is already running shows the existing window.

---

## 5. Example: a minimal client session

```js
const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;

await listen("devices", (e) => console.log("devices:", e.payload.devices.length));
await listen("busy", (e) => console.log("busy:", e.payload));

const info = await invoke("get_info");
if (!info.elevated) console.warn("Switching devices requires administrator rights");

const { devices } = await invoke("get_devices", { force: false });
const webcam = devices.find((d) => d.category === "camera" && d.present);
if (webcam) {
  const r = await invoke("set_device_enabled", { id: webcam.id, enabled: false });
  console.log(r.ok ? "off" : `${r.code}: ${r.message}`);
}
```
