# screenBlank

Tray app that makes screens look switched off, and can really turn them off,
even monitors that don't support sleep.

It covers each monitor with a pure black, always-on-top window, and can go
further per screen: put the monitor into its own standby, or cut its signal and
put your windows back afterwards.

Single exe, no installer needed, no admin rights, no dependencies.

## Windows

**Screens** (left-click the tray icon). A live map of your monitors, laid out
the way they sit on your desk, showing each one as *On*, *Dimmed*, *Blanked*
or *Off*.

- Click a screen to select it; double-click to blank or wake it.
- **On / Dim / Blank / Off** for the selected screen.
- **Turn off by:** how this screen's Off works (see below). Remembered per
  screen, by its name.
- **Dim strength** slider, live.
- **Blank all, Dim all, Wake all.**
- **Identify screens** shows each monitor's number and name on it.

**Settings** (tray menu or the Screens window). Hotkeys, wake behaviour,
auto-blank, look, power options, Start with Windows and the Explorer menu.

## Blank, Dim and Off

| | What happens | Wakes on |
| --- | --- | --- |
| **Blank** | Black screen (software only) | Double-click it, or On |
| **Dim** | Darkened, click-through, keep working | Undim / On |
| **Off** | Screen turned off using its *Turn off by* method | On / Wake all |

### Turn off by: making screens without sleep go off

| Method | Works on | Desktop |
| --- | --- | --- |
| **Auto** | The screen's own standby over DDC/CI if it has one (like Windows' monitor timeout); otherwise the black overlay only | Untouched |
| **Power off** | DDC/CI screens with a power-off mode: power light goes off too. **On** sends power-on, but many monitors (the Lenovo LT2452p included) only come back with their power button | Untouched |
| **Cut its signal** | Every screen, even without DDC/CI. With no signal, every monitor goes into its own standby | All screens flicker for a second; every window is put back on wake. If it's the primary screen the taskbar moves while it's off |
| **Black only** | Every screen: the black overlay, the monitor itself is never touched | Untouched |

Tested on a Lenovo LT2452p, which ignores Windows' monitor-off and only has a
DDC/CI *hard* power-off that it can't be woken from: *Cut its signal* turns it
fully off and puts all windows back, including maximized ones.

### Turning screens on without the power button

**On** always also sends the screen a DDC/CI "power on", so it brings back a
screen in standby even when screenBlank didn't put it there (for example
Windows' own monitor timeout). Everything screenBlank's Off does can be undone
this way; it never uses a monitor's hard power-off. A screen switched off with
its own power button usually stops answering DDC/CI, so that one still needs
the button.

## Explorer right-click menu

Right-click the desktop or an empty spot in a folder (Windows 11: *Show more
options*) for a **screenBlank** submenu:

- Blank this screen
- Turn this screen off
- Blank all except this screen
- Blank all screens
- Dim / undim all screens
- Turn all screens on
- Screens..., Settings...

"This screen" is the screen you right-clicked on. Turn it on or off in
Settings; `install.bat` adds it and `uninstall.bat` removes it.

## Hotkeys

| Default | Action |
| --- | --- |
| `Ctrl+Alt+B` | Blank / wake all screens |
| `Ctrl+Alt+O` | Blank all screens except the one the mouse is on |
| `Ctrl+Alt+M` | Blank / wake the screen the mouse is on |
| `Ctrl+Alt+D` | Dim / undim all screens |

Change them in Settings. If another app owns a hotkey, screenBlank says so.

## Other features

- **Wake modes:** any input; keyboard only; or *hotkey only*, which blocks
  every other key and click (Ctrl+Alt+Del still works).
- **Auto-blank when idle**, skipped while a video plays or a fullscreen app or
  presentation runs.
- **Lock Windows on wake.**
- **Faint moving clock** on blanked screens (burn-in safe).
- Fade in/out, hidden cursor, keep the PC awake while blanked, ignores mouse
  jigglers, swallows the wake key so it doesn't type into your app.
- Screens plugged in during Blank all get blanked too.
- screenBlank never changes a monitor's picture settings or input (those are
  saved to a settings memory that wears with every write). It only sends the
  standby/power-on command, and a cut signal is undone on wake, on exit, or on
  the next start after a crash.

## Command line

Commands go to the running instance; if none runs, screenBlank starts first.

```
screenBlank                  start in the tray (again: open the Screens window)
screenBlank blank            blank all screens
screenBlank wake             wake everything
screenBlank toggle           blank or wake all screens
screenBlank others           blank all screens except the mouse's
screenBlank cursor           blank or wake the mouse's screen
screenBlank monitor N        blank or wake screen N
screenBlank off-monitor N    turn screen N off with its off method
screenBlank on-monitor N     turn screen N back on
screenBlank off-cursor       turn the mouse's screen off
screenBlank on-all           wake everything and power every screen on
screenBlank install-menu     add the Explorer right-click menu (uninstall-menu removes it)
screenBlank dim              dim or undim all screens
screenBlank dim-monitor N    dim or undim screen N
screenBlank identify         show each screen's number
screenBlank screens          open the Screens window
screenBlank settings         open the Settings window
screenBlank settings-file    open settings.ini
screenBlank exit             quit
```

Screens are numbered left to right, as in the Screens window.

## Settings file

`%APPDATA%\screenBlank\settings.ini`, or a `settings.ini` next to the exe for
portable use. Everything in the Settings window is in there with comments,
plus the per-screen `[OffMethod]` section. Save it and screenBlank reloads it
within a few seconds. `ddc-restore.ini` next to it holds what to undo; leave
it alone.

## Install

```
install.bat
```

Builds the exe if needed, copies it to `%LOCALAPPDATA%\Programs\screenBlank`,
adds a Start Menu shortcut and starts it. Turn on Start with Windows in
Settings. `uninstall.bat` removes it all (settings are kept).

## Build from source

Visual Studio 2022 (or Build Tools) with "Desktop development with C++".

```
build.bat
```

| File | Purpose |
| --- | --- |
| `screenBlank.cpp` | The whole program |
| `screenBlank.rc`, `resource.h` | Screens and Settings windows, icon, version info |
| `screenBlank.manifest` | Modern controls, per-monitor DPI |
| `screenBlank.ico`, `make-icon.ps1` | Icon and the script that draws it |
| `build.bat`, `install.bat`, `uninstall.bat` | Build, install, remove |

## Limits

- **Blank alone can't switch off an LCD backlight.** Use Off with a method
  your screen supports.
- **Cut its signal** can't cut every screen at once; one stays connected and
  is blanked instead. Desktop icons may rearrange when the primary screen is
  cut. Windows of elevated (admin) apps can't be moved back by screenBlank.
- The Start menu, Ctrl+Alt+Del, UAC prompts and some system overlays sit above
  every app window and can show through a blanked screen.
- If screenBlank crashes, its black windows go with it, and the next start
  undoes any monitor changes.
