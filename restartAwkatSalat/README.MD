# Restart Awqat Salaat on Explorer Restart

This is a companion utility for [Awqat Salaat](https://github.com/Khiro95/Awqat-Salaat), a simple Windows widget that shows the time of the next prayer alongside a countdown on the taskbar.

When `explorer.exe` crashes or is restarted, the Awqat Salaat tray icon disappears from the taskbar and doesn't come back. This utility fixes that. A scheduled task watches for a new `explorer.exe` process, waits until the taskbar is ready, and then restarts Awqat Salaat so its icon registers again.

## How it works

1. **Trigger.** A Task Scheduler event trigger fires on Security event **4688** (a new process was created) when the new process is `C:\Windows\explorer.exe`.
2. **Wait for the taskbar.** `RestartAwqat.exe` polls for the taskbar window (`Shell_TrayWnd`) with a small PowerShell call. A running `explorer.exe` process is not enough, because the taskbar and tray appear a few seconds later. The script waits for the taskbar, pauses a moment, and checks again to confirm it is still there.
3. **Skip duplicates.** The script records the current `explorer.exe` PIDs in `%LOCALAPPDATA%\awqat-explorer-pid.txt`. If an event fires again for the same Explorer instance (including events caused by launching the app), it exits without doing anything.
4. **Restart the app.** It kills `AwqatSalaat.WinUI.exe`, waits one second, and launches the app through its `shell:AppsFolder` path.

## Files

| File | Purpose |
|------|---------|
| `RestartAwqat.exe` | The compiled restart script (built from `RestartAwqat.bat` with a bat-to-exe converter) |
| `RestartAwqat.xml` | Task Scheduler definition, including the event trigger |
| `install.bat` | Installs the exe and registers the task (asks for admin rights) |
| `uninstall.bat` | Removes the task and installed files (asks for admin rights) |
| `awkatSalat.ico` | Icon used when building the exe (not used by the installer) |

## Requirements

- Windows 10 or 11
- [Awqat Salaat](https://github.com/Khiro95/Awqat-Salaat) (WinUI) installed
- Administrator rights to install (the scripts show a UAC prompt on their own)

## Install

1. Keep `install.bat`, `RestartAwqat.exe`, and `RestartAwqat.xml` in the same folder.
2. Double-click `install.bat` and approve the UAC prompt.
3. When it prints `Installed`, the setup is done.

The installer does four things:

- Copies `RestartAwqat.exe` to `C:\ProgramData\AwqatRestart\`.
- Enables the **Process Creation** audit policy, which is required for event 4688 to be logged.
- Fills the exe path into the XML and saves a temporary UTF-16 copy. Your original XML is not modified.
- Registers the task `Restart Awqat on Explorer Restart` for the user who ran the installer.

## Uninstall

Double-click `uninstall.bat` and approve the UAC prompt. It removes the scheduled task, `C:\ProgramData\AwqatRestart\`, and the saved PID file. It does not turn the Process Creation audit policy back off, since other tools may rely on it. To disable it manually, run this in an admin prompt:

```
auditpol /set /subcategory:"{0CCE922B-69AE-11D9-BED3-505054503030}" /success:disable
```

## Verify and test

Check that the task exists:

```
schtasks /query /tn "Restart Awqat on Explorer Restart"
```

Run it manually:

```
schtasks /run /tn "Restart Awqat on Explorer Restart"
```

Test the real trigger: end `explorer.exe` in Task Manager, then use **File > Run new task**, enter `explorer.exe`, and confirm. After a few seconds the taskbar returns and Awqat Salaat should restart with its tray icon.

If a manual run does nothing, the PID file may be marking the current Explorer instance as already handled. Delete it and try again:

```
del "%LOCALAPPDATA%\awqat-explorer-pid.txt"
```

## Building the exe

`RestartAwqat.exe` is compiled from `RestartAwqat.bat` with a bat-to-exe converter. Recommended settings:

- **Invisible / hidden window**, so no console flashes when the task runs.
- **64-bit** build if the converter offers it.
- **Don't** set "run as administrator" in the exe manifest. The task runs as a normal user task, and an elevated exe fails to start from it.

Rebuild the exe after any change to the batch file, and run `install.bat` again to replace the installed copy.

## Troubleshooting

| Problem | What to check |
|---------|---------------|
| Task is not listed in Task Scheduler | Refresh the view (F5), then run the `schtasks /query` command above. If it is missing, run `install.bat` again and read the error it prints. |
| `Missing file` during install | `install.bat`, `RestartAwqat.exe`, and `RestartAwqat.xml` must be in the same folder, and the XML must not be named `RestartAwqat.xml.txt`. |
| Task never fires | Check that the Process Creation audit policy is enabled (`auditpol /get /subcategory:"{0CCE922B-69AE-11D9-BED3-505054503030}"`) and that Event Viewer logs event 4688 under **Windows Logs > Security** when Explorer starts. |
| Task runs but the app doesn't start | Check your antivirus quarantine (bat-to-exe files are often false positives). Also confirm the exe was built from the latest batch file. |
| Tray icon still missing after the restart | Increase the settle delay in the batch file (`timeout /t 3`), rebuild, and reinstall. |
| Console window flashes | Rebuild the exe with the invisible/hidden window option. |

## Notes

- The task also runs at sign-in, because Explorer starts then too. The app is restarted once after the taskbar is ready.
- The app is launched with `start ""` and not `explorer.exe shell:...`. Launching through `explorer.exe` creates another `explorer.exe` process, which fires event 4688 again.
- The app ID contains a `!`. Delayed expansion must be disabled around the launch line, or CMD strips the `!` and the launch fails.