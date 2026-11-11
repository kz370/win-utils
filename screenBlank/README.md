# screenBlank

Turn individual screens black, dim or off from the Windows tray, including
monitors that ignore Windows' own "turn off display".

- **Blank** a screen: pure black, instantly, nothing on the desktop moves.
- **Dim** a screen and keep working on it.
- **Turn a screen off** using whatever that monitor supports: its standby mode,
  a full power off, or cutting its signal (your windows are put back after).
- **Turn it back on** from the app, so you don't need the monitor's buttons.

One small exe. No installer required, no admin rights, nothing else to install.

---

## Quick start

1. Run `screenBlank.exe` (or `install.bat` to install it, see [Install](#install)).
   A monitor icon appears in the tray.
2. **Left-click the tray icon** to open the **Screens** window.
3. Click a screen in the map, then press **Blank**, **Dim**, **Off** or **On**.

That's it. Everything else is optional.

---

## The Screens window

The top of the window is a map of your monitors, laid out the way Windows
arranges them. Each tile shows the screen's number, its name (e.g. *HP P24h G5*)
and what it's doing: **On**, **Dimmed**, **Blanked**, **Off**, **Powered off**
or **Off (signal cut)**.

| Do this | To |
| --- | --- |
| Click a screen | Select it |
| Double-click a screen | Blank it, or wake it if it's blanked |
| Right-click a screen | Get every option for that screen |
| **On / Dim / Blank / Off** | Change the selected screen |
| **Off by** dropdown | Choose how *this* screen turns off (remembered per screen) |
| **Dim** slider | How dark Dim is (live) |
| **Blank all / Dim all / Wake all** | All screens at once |
| **Identify** | Shows each screen's number and name on the screen itself |

The right-click menu also has **Power on (wake it from standby)**, which wakes a
screen that went to standby on its own, for example from Windows' display
timeout.

---

## Blank, Dim and Off

| | What you see | What happens to the monitor | How to get it back |
| --- | --- | --- | --- |
| **Blank** | Pure black | Nothing. A black window covers it | Double-click it, **On**, or **Wake all** |
| **Dim** | Darker, still usable, clicks go through | Nothing | **On**, or Dim again |
| **Off** | Screen off | Depends on its *Off by* method, below | **On**, or **Wake all** |

On an LCD, Blank and Dim can't turn the backlight off, so a black screen still
glows faintly in a dark room. For fully dark, use **Off**.

### Off by: how a screen turns off

Pick this per screen in the Screens window. screenBlank remembers it by the
screen's name.

| Method | What it does | Coming back on | Desktop |
| --- | --- | --- | --- |
| **Auto** *(default)* | Puts the monitor in its own **standby** (backlight off, power light usually turns orange). Screens without a standby mode just go black | **On** wakes it | Nothing moves |
| **Power off** | Tells the monitor to **power off** completely, power light off too | **On** sends power-on, but **many monitors only come back with their power button** | Nothing moves |
| **Cut signal** | Windows stops sending the screen a picture, exactly like unplugging its cable. Every monitor goes to standby with no signal | **On** reconnects it and puts every window back where it was | All screens flicker for a second. If it's your primary screen, the taskbar and desktop icons move to another screen while it's off |
| **Black only** | Black window only | Instant | Nothing moves |

**Which one should I use?**

- Most monitors: **Auto**. Fully dark, desktop untouched, wakes instantly.
- A monitor that stays lit on Auto (it has no standby mode): **Cut signal**.
  It works on every monitor. To stop the taskbar jumping, make a different
  screen your main display (Windows Settings → System → Display → *Make this my
  main display*).
- **Power off** only if you can reach that monitor's power button, or you've
  checked that **On** brings it back.

---

## Other ways to control it

### Tray icon

- **Left-click**: open the Screens window.
- **Right-click**: Blank all, Blank all except this screen, Blank this screen,
  Dim all, per-screen Blank and Dim, Identify, Wake on, Dim strength,
  Auto-blank when idle, Options, Start with Windows, Settings, Exit.

### Explorer right-click menu

Right-click the desktop or an empty spot in any folder and choose **screenBlank**
(Windows 11: *Show more options* first). "This screen" means the screen you
right-clicked on.

- Blank this screen
- Turn this screen off
- Blank all except this screen
- Blank all screens
- Dim / undim all screens
- Turn all screens on
- Screens..., Settings...

Turn it on or off in **Settings → Explorer right-click menu**.

### Hotkeys

| Default | Action |
| --- | --- |
| `Ctrl+Alt+B` | Blank all screens / wake them |
| `Ctrl+Alt+O` | Blank every screen except the one the mouse is on |
| `Ctrl+Alt+M` | Blank / wake the screen the mouse is on |
| `Ctrl+Alt+D` | Dim / undim all screens |

Change them in Settings. If another program already uses a hotkey, screenBlank
tells you.

### Command line

Useful for shortcuts, Stream Deck buttons and scripts. Commands go to the
running screenBlank; if it isn't running, it starts first.

```
screenBlank                  start in the tray (if already running: open the Screens window)
screenBlank blank            blank all screens
screenBlank wake             wake everything
screenBlank toggle           blank or wake all screens
screenBlank others           blank all screens except the mouse's
screenBlank cursor           blank or wake the mouse's screen
screenBlank monitor N        blank or wake screen N
screenBlank dim              dim or undim all screens
screenBlank dim-monitor N    dim or undim screen N
screenBlank off-monitor N    turn screen N off (using its Off by method)
screenBlank on-monitor N     turn screen N back on
screenBlank off-cursor       turn the mouse's screen off
screenBlank on-all           wake everything and power every screen on
screenBlank identify         show each screen's number
screenBlank screens          open the Screens window
screenBlank settings         open the Settings window
screenBlank settings-file    open settings.ini
screenBlank install-menu     add the Explorer right-click menu
screenBlank uninstall-menu   remove it
screenBlank exit             quit
```

Screens are numbered left to right, the same as in the Screens window.

---

## Waking up after Blank all

After **Blank all**, screens wake on input. Choose what counts in
**Settings → Waking**:

- **Any key, click or mouse move** (mouse must move a set distance, 30 px by
  default, so a bumped desk doesn't count)
- **Keyboard only**
- **Hotkey only**: every other key and click is blocked until you press the
  Blank all hotkey. `Ctrl+Alt+Del` always works.

Input is ignored for about a second after blanking (longer when started from the
tray or a button) so the click that started it doesn't wake it. The key that
wakes the screens isn't passed on to the app underneath.

Screens blanked one at a time don't wake on input; double-click them or press On.

---

## Settings

Open from the tray menu or the **Settings...** button. **Apply** lights up once
you change something.

| Section | Options |
| --- | --- |
| Hotkeys | The four hotkeys above (Backspace clears one) |
| Waking | Wake on, mouse distance, don't pass the wake key to apps, ignore mouse jigglers and other synthetic input, lock Windows on wake |
| Auto-blank | Blank all screens after N idle minutes; skip it while a video or fullscreen app/presentation runs |
| Look | Fade in and out, hide the mouse cursor on blank screens, faint moving clock on blank screens |
| Power | Keep the PC awake while blanked, ask Windows to turn monitors off after Blank all, put blanked monitors in standby |
| | Start with Windows, Explorer right-click menu |

Everything is stored in `%APPDATA%\screenBlank\settings.ini`, with comments.
Edit and save it and screenBlank picks up the change within a few seconds. To
run portable, put a `settings.ini` next to `screenBlank.exe`.

---

## Is it safe for my monitors?

screenBlank is built to never wear out or damage a monitor:

- **Blank, Dim, Black only**: software only. Nothing is sent to the monitor.
- **Auto (standby)** and **On**: the same standby / wake command Windows uses
  for its own display timeout. Monitors don't store it.
- **Cut signal**: the same as unplugging the cable. Nothing is sent to the monitor.
- **Power off**: a standard monitor command. It can't harm the monitor, but some
  monitors then need their power button to turn back on.
- screenBlank **never changes brightness, contrast, colours or the input**.
  Monitors save those to a small memory chip that wears a little with every
  write.

If screenBlank is closed or crashes, its black windows disappear with it, and
the next start reconnects any screen whose signal was cut.

---

## Troubleshooting

| Problem | Fix |
| --- | --- |
| A screen with a cut signal didn't come back | Start screenBlank again (it reconnects automatically), or press `Win+P` → *Extend* |
| A screen stays off after **Power off** | Press its power button. Use Auto or Cut signal for that screen instead |
| A screen still glows when "off" | It has no standby mode. Switch it to **Cut signal** |
| A screen disappeared from Windows after the display timeout | Some monitors (DisplayPort) fully disconnect in deep sleep. `Win+P` → *Extend* brings it back |
| A hotkey doesn't work | Another app uses it. Pick another in Settings |
| The Start menu shows through a black screen | The Start menu, `Ctrl+Alt+Del` and UAC prompts sit above every app |

A log of what screenBlank did to each screen is in
`%APPDATA%\screenBlank\screenBlank.log`.

---

## Install

```
install.bat
```

Copies `screenBlank.exe` to `%LOCALAPPDATA%\Programs\screenBlank` (building it
first if needed), adds a Start Menu shortcut and the Explorer right-click menu,
and starts it. Turn on **Start with Windows** in Settings.

`uninstall.bat` removes the program, the shortcut, the Explorer menu and the
autostart entry. Your settings in `%APPDATA%\screenBlank` are kept.

You can also just run `screenBlank.exe` from anywhere.

## Build from source

Needs Visual Studio 2022 or the Build Tools, with "Desktop development with C++".

```
build.bat
```

| File | What it is |
| --- | --- |
| `screenBlank.cpp` | The whole program |
| `screenBlank.rc`, `resource.h` | Screens and Settings windows, icon, version info |
| `screenBlank.manifest` | Modern controls, sharp on high-DPI screens |
| `screenBlank.ico`, `make-icon.ps1` | The icon and the script that draws it |
| `build.bat`, `install.bat`, `uninstall.bat` | Build, install, remove |

Windows 10 or 11.
