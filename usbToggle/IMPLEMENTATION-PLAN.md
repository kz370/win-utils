# USB Toggle Utility — Implementation Plan

**Status:** Proposal — implementation is pending approval.

## Goal and constraints

Build a small Windows 10/11 tray utility using C#/.NET 8 and WPF. Use WPF-UI for the Fluent/Windows 11 experience and tray icon, CommunityToolkit.Mvvm for MVVM, and `System.Text.Json` for configuration. Publish framework-dependent. The app controls PnP device enablement; it does not cut USB port power.

No cross-platform support, Electron/web UI, or other unrequested features are in scope.

## 1. Proposed solution and project structure

```text
usbToggle\
  usbToggle.sln
  usbToggle\
    App.xaml
    App.xaml.cs
    app.manifest
    Models\
      DeviceRecord.cs
      DeviceIdentity.cs
      DeviceSnapshot.cs
      DeviceGroup.cs
      AppSettings.cs
      ConfigDocument.cs
    Services\
      SetupApiDeviceCatalog.cs
      DeviceChangeMonitor.cs
      PnpDeviceController.cs
      JsonConfigStore.cs
      StartupTaskService.cs
      AppLog.cs
    ViewModels\
      MainWindowViewModel.cs
      DeviceRowViewModel.cs
      DeviceGroupViewModel.cs
      SettingsViewModel.cs
    Views\
      MainWindow.xaml
      SettingsPage.xaml
    Assets\
      TrayIcon.ico
```

Target `net8.0-windows` with WPF enabled and a Windows-only manifest. `MainWindowViewModel` owns the observable devices and groups shared by the window and tray menu. Services own device enumeration, device operations, notifications, configuration, startup registration, and logging. Keep composition in app startup; avoid adding a DI framework or background service host unless implementation proves one necessary.

## 2. NuGet packages

- **`Wpf.Ui`** — Fluent styling, Mica/backdrop support, `ToggleSwitch`, and `NotifyIcon`.
- **`CommunityToolkit.Mvvm`** — observable properties and commands for view models.
- **No runtime `System.Management` package** — use Windows device-notification and PnP APIs directly.
- **No separate JSON package** — use .NET 8 `System.Text.Json`.
- **No logging package** — a small app-specific rolling file logger is sufficient.

Use test-only packages in a test project if needed; do not add them to the application runtime.

## 3. Data model and JSON configuration

Persist user names, groups, identity hints, and last-seen information—not a cached enabled state. The live toggle state must come from Windows on each scan.

A device record contains a stable app ID, device/hub kind, optional custom name, VID/PID, optional serial, location path, interface discriminator where available, last observed `InstanceId` values, parent-hub ID, and last-seen time. A custom group contains a name and member device IDs. Hub-child relationships are rebuilt from the current PnP tree.

Example schema:

```json
{
  "schemaVersion": 1,
  "settings": {
    "theme": "System",
    "startWithWindows": false,
    "startInTray": false
  },
  "devices": [
    {
      "id": "device:...",
      "kind": "Device",
      "customName": "Webcam",
      "identity": {
        "vid": "046D",
        "pid": "0825",
        "serial": "ABC123",
        "locationPath": "..."
      },
      "lastInstanceIds": ["USB\\VID_046D&PID_0825\\ABC123"],
      "parentHubId": "hub:...",
      "lastSeenUtc": "2026-11-02T20:00:00Z"
    }
  ],
  "groups": [
    {
      "id": "group:...",
      "name": "Privacy mode",
      "deviceIds": ["device:..."]
    }
  ]
}
```

Store the file at `%AppData%\usbToggle\config.json`. If missing, create defaults and perform an initial scan. If malformed, preserve the original as a timestamped backup, show a visible error, and start with defaults rather than silently discarding it. Save via a temporary file followed by atomic replacement. Use sequential schema migrations; do not overwrite a file with an unsupported future schema version.

## 4. Device discovery, matching, and parent tree

Use **SetupAPI and Configuration Manager**, rather than WMI polling:

- Enumerate installed PnP devices with `SetupDiGetClassDevsW` using `DIGCF_ALLCLASSES`, first including `DIGCF_PRESENT` for the live list and then without it to retrieve Windows-retained, non-present devices.
- Enumerate records with `SetupDiEnumDeviceInfo`; retrieve instance IDs with `SetupDiGetDeviceInstanceIdW`.
- Retrieve friendly name, hardware IDs, service, location paths, container ID, and presence using `SetupDiGetDevicePropertyW` / `SetupDiGetDeviceRegistryPropertyW`.
- Read devnode presence and problem state with `CM_Get_DevNode_Status`; use `CM_Get_Parent` and `CM_Get_Device_IDW` to walk from each device to its ancestors.
- Identify hub devnodes from their PnP properties/service (`USBHUB3`/`USBHUB`) and construct the live hub-to-device tree. Put devices without a recognized hub under an “Other USB devices” group.
- Parse VID/PID from USB hardware IDs. Use a serial when Windows exposes one in device properties or the USB instance-ID format; otherwise mark it unavailable. Do not treat an arbitrary generated instance-ID suffix as a serial.

### Matching policy

1. Match by normalized VID/PID plus serial when available, adding an interface discriminator when needed.
2. Without a serial, match by VID/PID plus USB location path (and interface discriminator where applicable).
3. Identify hubs by their own PnP identity and location path, so two identical hubs connected at different ports remain distinct.
4. If multiple live devices match one identity, mark it ambiguous and disable its action until resolved; never pick an arbitrary device.

Do not use `InstanceId` as the persistent key. It is the current operation target and a recorded historical alias. When a no-serial device moves ports, its location path—and often its `InstanceId`—changes. Safely show the old entry as missing and the moved device as a new entry instead of risking control of the wrong hardware. Windows-retained PnP records plus the app’s last-seen records provide the previously connected list; Windows cleanup may remove older system records.

For live changes, register a device-interface notification using `RegisterDeviceNotificationW` for USB device/hub interfaces and handle `WM_DEVICECHANGE` arrival/removal notifications. Debounce bursts, rescan, and update the shared view model. Keep a manual scan button; rescan after each completed device action.

## 5. Device actions and groups

Define `IDeviceController` with enable/disable operations keyed by the **current, uniquely matched `InstanceId`**. The first implementation runs Windows PowerShell 5.1 with the PnPDevice cmdlets:

```powershell
Disable-PnpDevice -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
Enable-PnpDevice  -InstanceId $instanceId -Confirm:$false -ErrorAction Stop
```

Send the ID as JSON on redirected standard input to a fixed script, not by concatenating it into script text. Run non-interactively, capture errors, and enforce a timeout. Verify state by rescanning PnP state rather than trusting process exit alone.

A hub switch targets the hub devnode itself. Its children can disappear while disabled; after enabling, rescan to rebuild the tree. Confirm before disabling a hub to reduce the chance of disconnecting a keyboard or mouse. Do not target USB host-controller nodes.

Custom-group switches operate on member devices sequentially. Derive group state from members: all on, all off, or mixed. Continue after a member failure and show the result for each affected device.

### Optional USB storage switch

The optional “Block USB storage” switch writes `HKLM\SYSTEM\CurrentControlSet\Services\USBSTOR\Start` (`4` to block, `3` to unblock). It requires elevation and is system-wide, and may not immediately stop already active storage devices. Defer it from the initial implementation unless approved; clearly explain its impact in the UI.

## 6. Tray and window lifecycle

- Use WPF-UI’s `NotifyIcon`. Left click shows/hides the main window; its context menu contains the same named device/group toggles plus **Open** and **Exit**.
- Bind tray items to the same row/group view models and commands as the window. Do not maintain separate tray state.
- Closing the window hides it. **Exit** performs application shutdown, disposes device notifications and the tray icon, and releases single-instance resources.
- Use a per-user named mutex. A second launch sends an activation request to the first instance over a named pipe, which brings the window forward.
- Parse `--tray` to start hidden. `startInTray` applies to configured startup; `--tray` overrides the current launch.
- Register/unregister an app-specific Task Scheduler logon task when **Start with Windows** changes. Run the app at highest privileges for the interactive user; pass `--tray` when appropriate.

### Elevation recommendation

Use a `requireAdministrator` manifest and a scheduled logon task for normal automatic startup. The first manual launch (and task setup) requires UAC; after that, the task starts the tray-resident app elevated at logon without a prompt each session. If the task is disabled, manually launching the executable still prompts for elevation. This is simpler and safer than creating a separate unelevated launcher/broker while meeting the no-repeat-prompt goal for the usual tray-resident workflow.

## 7. UI, errors, and logging

Use a Fluent main window with device search, scan, inline rename, expandable hub groups, custom groups, and clear enabled/disabled/missing/error states. Provide settings for system/light/dark theme, startup, start-in-tray, and config location. Use WPF device-independent sizing, keyboard-accessible controls and focus order, and a Per-Monitor V2 manifest. Apply Mica on Windows 11 and a non-Mica Fluent fallback on Windows 10.

Represent operation failures per device rather than crashing or reverting to a success-shaped state. Distinguish access denied, device missing, timeout, ambiguity, and other PnP errors. Show an actionable inline message and keep the last scan result visibly stale until refreshed.

Write a small rolling log under `%LocalAppData%\usbToggle\logs\`, capped by size and file count. Log operation, result, and diagnostic details without serials or full device identifiers. If logging itself fails, report that once in the UI and continue without claiming the error was logged.

Include this limitation in About/help text: **Disabling a device unloads its driver, but the USB port may remain powered.**

## 8. Implementation milestones

1. **Create the WPF shell and package setup.**  
   **Done when:** `usbToggle` builds as `net8.0-windows`, starts, and shows the WPF-UI window.
2. **Implement config models and persistence.**  
   **Done when:** defaults, atomic saves, corrupt-file handling, and schema-version checks work.
3. **Implement PnP discovery and identity matching.**  
   **Done when:** present and retained USB devices are listed with status, properties, and parent hub where available; ambiguous matches are surfaced safely.
4. **Add device-change notifications and refresh.**  
   **Done when:** plug/unplug events update the view without manual scanning and scans can also be requested manually.
5. **Add device and hub controller actions.**  
   **Done when:** enable/disable results are verified by a rescan and failures appear per device without taking down the app.
6. **Add custom names, groups, search, and status UI.**  
   **Done when:** renames persist; group actions report partial failures; hub groups expand and collapse.
7. **Add tray menu and lifecycle.**  
   **Done when:** tray and window show identical state; hide/show, Open, Exit, `--tray`, and second-instance activation work.
8. **Add elevation and startup settings.**  
   **Done when:** the scheduled task can be registered and removed, starts at logon with the selected tray behavior, and device actions run elevated.
9. **Add optional USB storage control only if approved.**  
   **Done when:** the registry toggle reports permission failures and accurately documents that active devices may remain connected.
10. **Polish accessibility, DPI behavior, and About text.**  
    **Done when:** keyboard operation and layout are verified at 100%, 150%, and 200% DPI, and Windows 10 uses the intended backdrop fallback.

## 9. Manual test plan

- Plug in and remove a USB hub; check that it appears/disappears and its child tree refreshes.
- Use a no-serial device; verify it is matched by location path and is not confused with an identical model on another port.
- Connect two identical devices with distinct serials; verify separate names and toggles.
- Connect two identical no-serial devices on different ports; verify distinct location-based records.
- Move a no-serial device to another port; verify the old record becomes missing and the new location is not incorrectly assigned the old device’s identity.
- Disable a device, then unplug it; verify the UI reports it missing and does not claim the disable succeeded on the absent device.
- Attempt an operation without elevation; verify the access-denied state is visible and the app remains usable.
- Disable and re-enable a hub; verify child states refresh and the hub confirmation appears.
- Exercise a custom group with one unavailable member; verify the other members are processed and the partial failure is reported.
- Test tray/window state synchronization, close-to-tray, Exit, `--tray`, startup task registration, and second-instance activation.
- Corrupt the JSON file and test a future schema version; verify data is preserved and the failure is surfaced.
- Verify keyboard navigation and 100%, 150%, and 200% DPI layouts on Windows 10/11.

## 10. Risks and open questions

- A no-serial device cannot be reliably recognized after moving ports; this plan favors safe non-matching over guessing.
- Disabling hubs can disconnect input devices or multiple downstream devices; confirmation reduces but cannot eliminate that risk.
- Windows-retained PnP history is subject to OS cleanup.
- The USB storage registry switch is system-wide and may not immediately affect already active devices; defer it unless it is explicitly included in the first release.
- The scheduled-task approach avoids repeated prompts for logon startup, but manually launching the app still requires UAC.

No blocking questions are required to proceed with the proposed plan. Approval is required before implementation begins.
