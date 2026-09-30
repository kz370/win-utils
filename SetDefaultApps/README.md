# Set Default Apps

One double-click to make your favourite apps the default for all their file types on Windows 10/11 (for example PotPlayer for every video and audio type, FastStone for every image type), instead of setting each extension by hand in Windows Settings.

It changes defaults for the **current user only**. No admin rights needed.

---

## Quick start

1. Install the apps you want (PotPlayer, FastStone, ...).
2. *(PotPlayer only, recommended)* Open PotPlayer, press `F5`, go to **General > File Association**, select the video/audio types and click **Apply**. This makes PotPlayer register its own per-type icons, which the tool reuses.
3. Double-click **`SetDefaultApps.exe`**.
4. Read the summary for each app:
   ```
   === PotPlayer ===
   Changed: 53   Skipped (already set): 0   Failed: 0
   ```
5. If icons look stale, sign out and back in, or restart `explorer.exe`.

Running it again is safe and fast: extensions that are already set are skipped.

> Windows SmartScreen or antivirus may warn about an unsigned exe that starts PowerShell. Choose **More info > Run anyway**, or build the exe yourself (see below) and read the source first.

---

## What is in this folder

| File | Purpose |
|---|---|
| `SetDefaultApps.exe` | The finished tool (if you have built it) |
| `apps.ini` | **The only file you normally edit**: which app gets which file types |
| `build.bat` | Builds `SetDefaultApps.exe` from the files below |
| `run.ps1` | The logic: reads `apps.ini` and sets the defaults |
| `SFTA.ps1` | [PS-SFTA](https://github.com/DanysysTeam/PS-SFTA) by Danysys (MIT licence), does the actual default-app change |
| `SetDefaultApps.cs` | Tiny launcher that runs `run.ps1` from inside the exe |
| `*.ico` | *(optional)* your own icon for the exe |

---

## Changing or adding apps (`apps.ini`)

Every app is a block. Lines starting with `#` are comments.

```ini
[FastStone Image Viewer]
exe = %ProgramFiles(x86)%\FastStone Image Viewer\FSViewer.exe
exe = %ProgramFiles%\FastStone Image Viewer\FSViewer.exe
extensions = jpg jpeg png gif bmp webp
extensions = cr2 nef arw dng
```

| Key | Meaning |
|---|---|
| `[Name]` | Any name, shown in the output |
| `exe` | Where the program is installed. Repeat the line to try several places; the first one found is used. If none exists, the app is skipped. Variables like `%ProgramFiles%` work |
| `extensions` | File types separated by spaces (with or without the dot). Repeat the line to keep lines short |
| `enabled` | `yes` or `no` (default `yes`). Use `no` to switch a block off |
| `icon-prefix` | *(optional)* Start of the names the app registers for its own file types, e.g. `PotPlayer`. The tool then uses the app's own per-type icons |
| `progid` | *(optional)* Choose the ProgID name yourself. Normally generated for you |
| `type = ProgID` | *(advanced)* Any other `name = value` line means "this file type uses this ProgID", e.g. `docx = Word.Document.12` |

**To add a new app:** copy a whole block, change the name, the `exe` path and the `extensions`.

**Microsoft Office and PDF:** ready-made blocks for Word, Excel, PowerPoint and Acrobat are included, switched off. Change `enabled = no` to `enabled = yes` to use one. Their ProgIDs are untested, so check them first (next section). Windows may refuse to change the `.pdf` default from a script.

**Finding a ProgID:** set the default by hand once for one file of that type, then in PowerShell:

```powershell
. .\SFTA.ps1
Get-FTA .docx
```

**Applying a change without rebuilding:** put your edited `apps.ini` in the same folder as `SetDefaultApps.exe`. It is used instead of the copy built into the exe.

---

## Building the exe yourself

Needs only Windows (it uses the C# compiler that ships with .NET Framework 4.x).

1. Put `build.bat`, `run.ps1`, `apps.ini`, `SetDefaultApps.cs` and (optionally) `SFTA.ps1` in one folder. If `SFTA.ps1` is missing, `build.bat` downloads it.
2. *(Optional)* Drop an `.ico` file in the same folder to give the exe your icon.
3. Double-click `build.bat`.

You get `SetDefaultApps.exe` with `apps.ini`, `run.ps1` and `SFTA.ps1` embedded inside it. Rebuild whenever you change `apps.ini` and want the change built in. If Explorer keeps showing the old icon, rename the exe or restart `explorer.exe`.

---

## How it works

1. Reads `apps.ini`.
2. For each enabled app, finds the exe. Not installed means the app is skipped.
3. Creates a ProgID for the app under `HKCU\Software\Classes` (current user only), unless the app or `apps.ini` already names one.
4. For every extension, checks the current default with `Get-FTA`. If it is already the right one, it skips it. Otherwise it calls `Set-FTA` (PS-SFTA), which writes the Windows UserChoice entry correctly.
5. Refreshes the icon cache.

---

## Troubleshooting

| Problem | What to try |
|---|---|
| `X was not found. Skipping.` | The exe path in `apps.ini` is wrong for your install. Fix or add an `exe =` line |
| Files open in the app but show a generic icon | Register the app's own file types in its settings first, then set `icon-prefix` and run again |
| `Failed: ...` on some extensions | Windows protects a few types (for example `.pdf`, `http`, `https`). Others can fail after a Windows update changes how defaults are stored |
| Icons did not update | Sign out/in, or restart `explorer.exe` |
| `build.bat` says csc.exe not found | Turn on **.NET Framework 4.8 Advanced Services** in Windows Features |
| `build.bat` build failed | Close `SetDefaultApps.exe` if it is running and try again |

**Undoing it:** open **Settings > Apps > Default apps** and choose another app for the file types, or delete the ProgID keys this tool created under `HKCU\Software\Classes` (their names are `SetDefaultApps.<AppName>` unless you set `progid`).

---

## Notes and limits

- Changes only affect the current Windows user.
- The Word/Excel/PowerPoint/Acrobat blocks are examples and have not been verified.
- Uses an unofficial method (PS-SFTA) to set defaults, because Windows 10/11 block the official one for scripts. A future Windows update could break it.

## Credits

Default-app logic: [PS-SFTA](https://github.com/DanysysTeam/PS-SFTA) by Danyfirex and Dany3j, MIT licence.
