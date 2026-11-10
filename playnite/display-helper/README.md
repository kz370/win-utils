# Display Helper 1.30.1 (modified)

A modified build of the Playnite add-on **Display Helper**. It adds the features below to the original version.

## Install

1. Close Playnite completely, including the tray icon.
2. Double-click `DisplayHelper_1.30.1.pext`.
3. If Playnite won't replace your current version, uninstall Display Helper in **Add-ons** first, then install the file again. Your settings are kept.

## What's new

### Real monitor names and Windows numbers
Monitors are listed by their real names and by the same number Windows Settings shows, e.g. `HP P24h G5 (Display 2)`. Before, they were listed by the graphics card name and `\\.\DISPLAY1`.

To check which screen is which, click **Identify** in **Add-ons → Extensions settings → Generic → Display Helper → Monitor names**. Each screen shows its number and name for a few seconds. When a monitor is turned on or off, the names and numbers update on their own.

### Custom monitor names
Give each monitor your own name.

1. Go to **Add-ons → Extensions settings → Generic → Display Helper → Monitor names**.
2. Type a name in the **Custom name** box next to a monitor. Leave the box empty to use the real name.
3. Click **Save**.

The monitor then shows as `Left Gaming Monitor (LEN LT2452pwC, Display 3)`. The name stays with that monitor even if its number changes.

### Open games on another monitor without changing the primary display
Before, Display Helper made the chosen monitor the primary display while the game ran, which moved the taskbar and desktop icons. Now it keeps your primary display and moves the game window to the chosen monitor as a borderless window.

1. Right-click a game and choose **Display Helper → Display**, then pick a monitor.
2. Set the game to **windowed** or **borderless** in its own video options. Exclusive fullscreen can't be moved.
3. Launch the game.

To choose how this works:
- **Global setting:** the checkbox *"When a game targets another display, move its window there as borderless…"* in the add-on settings. It's on by default.
- **Per game:** right-click the game and choose **Display Helper → Window mode**:
  - *Move window to chosen display (borderless)*
  - *Switch primary display* (the old behavior; use this for exclusive-fullscreen games)
  - *Use global setting*

### See a game's settings
Right-click a game and choose **Display Helper → Show current settings**. It shows a table of the game's display, window mode, resolution and refresh rate. Each value is tagged *This game*, *Global* or *Default* to show where it comes from. Below the table is a short "When you launch" list of what will happen.

When one game is selected, the Display Helper submenus put a ✓ next to the options chosen for that game.

### Other changes
- **Display → Clear display (use global setting)** removes a game's monitor choice.
- Games on mapped network drives and games protected by DRM are now detected.

## Notes
- The window is moved the moment it appears, but some games may still flash briefly on the primary monitor.
- With the setting *"only if no other game is running"* turned on, nothing is changed when Playnite thinks another game is already running.
- Logs: `<Playnite folder>\extensions.log`. Look for lines containing `WindowMover`.
