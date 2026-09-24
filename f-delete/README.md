# fdel — force delete

A tiny (~65 KB) Windows command-line tool that deletes a file or folder **even when another running app has it open** — without you having to hunt down and close that app first.

It is **safe by default**: it never touches protected Windows/system locations, and when a target is locked it *tells you which apps are holding it* and lets you decide how far to go. It never reboots your PC.

## Usage

```
fdel <path> [more paths...] [options]
```

Examples:

```
fdel "C:\Users\me\Downloads\stuck file.pdf"
fdel "D:\old project" -y
fdel report.xlsx --close
```

### Options

| Option | Meaning |
|--------|---------|
| `-y` | Delete without the confirmation prompt |
| `--close` | If locked, ask the holding app(s) to close normally |
| `--kill` | If still locked, end the holding process(es) |
| `--force` | Force-close the file handles **(advanced — may corrupt the other app's open data; last resort)** |
| `-h` | Help |

With `-y` and no escalation flag, fdel reports which processes hold the file and then **stops** rather than doing anything risky on its own.

## What it does, step by step

1. Clears ReadOnly / Hidden / System attributes.
2. Tries a normal delete.
3. If the target is locked, uses the Windows **Restart Manager** to identify and name the processes holding it.
4. Lets you escalate, in order of increasing risk:
   - **Close** the app(s) normally (sends a close request),
   - **End** the process(es),
   - **Force-close** the file handles inside them (advanced; the app keeps running but loses the handle, which can corrupt its open data).
   Each step is followed by a retry of the delete.
5. Otherwise it stops and leaves the file for you to delete later.

## Safety

- **Protected locations are refused outright:** the drive root, `C:\Windows`, `C:\Program Files`, `C:\Program Files (x86)`, `C:\ProgramData`, `C:\Users` (the whole tree), and the boot/recovery/system folders. Your own files *inside* your user profile (Downloads, Desktop, Temp, etc.) are allowed.
- **It never reboots the machine.**
- The force-close step always requires an explicit choice (or the `--force` flag) and, when interactive, typing `FORCE` to confirm.

## Requirements

- Windows 10/11, .NET Framework 4.x (built into Windows).
- **Run as Administrator** to act on files held by other users or by services. The app is manifested to request elevation automatically.

## Building from source

Requires the .NET Framework C# compiler that ships with Windows (no SDK needed):

```
build.bat
```

or directly:

```
%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe ^
  -optimize+ -platform:x64 -win32icon:fdel.ico -win32manifest:fdel.manifest ^
  -out:fdel.exe fdel.cs
```
