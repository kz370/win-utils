# cpulimit

Windows CLI that runs a command under a hard CPU usage cap.

It uses a Windows [job object](https://learn.microsoft.com/windows/win32/procthread/job-objects)
with CPU rate control. The command runs inside that job, so the cap also
applies to any child processes it spawns. `cpulimit` exits with the same
exit code as the command it ran.

## Usage

```
cpulimit <percent> <command> [arguments...]
```

- `<percent>` — integer 1–100.
- `<command>` — program to run, plus its arguments.

```powershell
cpulimit 20 ffmpeg -i input.mkv -c:v libx264 output.mp4
cpulimit 50 python train.py --epochs 10
cpulimit 5 cmd /c "npm run build"
```

### What percent means

`<percent>` is a share of **total CPU across all cores**, not one core.

On a 12-core machine, a single-threaded program never uses more than ~8% of
total CPU, so a cap above that has no effect on it. To limit a program to
roughly one full core, use `100 / <core count>`. Check your core count with:

```powershell
[Environment]::ProcessorCount
```

## Install

```
install.bat
```

What it does:

1. Builds `cpulimit.exe` first if it isn't already built.
2. Copies `cpulimit.exe` to `%LOCALAPPDATA%\Programs\cpulimit`.
3. Adds that folder to your user PATH (registry, `HKCU\Environment`).

No admin rights needed, nothing outside your user account is touched. Open a
new terminal afterward — existing terminals keep the old PATH. After that,
`cpulimit` works from any shell without a full path or `.exe`.

To install elsewhere: `install.bat "D:\Tools\cpulimit"`

### Uninstall

```
uninstall.bat
```

Removes the install folder and undoes the PATH change. Pass the same custom
path if you installed to a non-default location.

## Build from source

Requires Visual Studio 2022 (or Build Tools) with the "Desktop development
with C++" workload.

```powershell
build.bat
```

Finds the toolchain with `vswhere`, compiles `cpulimit.rc` (icon + version
info) with `rc`, then compiles and links `cpulimit.cpp` with `cl` into
`cpulimit.exe`.

## Files

| File | Purpose |
| --- | --- |
| `cpulimit.cpp` | Source — the whole program. |
| `cpulimit.rc` / `cpulimit.ico` | Icon and version info embedded in the exe. |
| `build.bat` | Compiles `cpulimit.exe`. |
| `install.bat` | Copies the exe and adds it to your PATH. |
| `uninstall.bat` | Reverses the install. |

## Limits

- Windows 8 / Server 2012 or newer — job object CPU rate control isn't
  available on older versions.
- The cap only exists while `cpulimit.exe` is running. If you kill
  `cpulimit`, the still-running child process keeps running uncapped.
- Job objects, and this cap, are a Windows-only mechanism — no Linux/macOS
  build.
