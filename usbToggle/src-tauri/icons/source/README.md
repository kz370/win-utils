# Icon sources

Change every USB Toggle icon (app window, taskbar, Start menu, tray, installer,
exe and the header logo) with **`set-icons.bat`** in the `usbToggle` folder:

```bat
set-icons.bat my-icon.png          rem one image for everything (or drag the image onto set-icons.bat)
set-icons.bat app.png tray.png     rem a separate image for the tray
set-icons.bat --reset              rem back to the built-in artwork
set-icons.bat                      rem regenerate from the files in this folder
```

Then run `build.bat`.

Use a square PNG (512×512 or larger) or SVG with a transparent background.

## Files

| File | Used for |
| --- | --- |
| `icon.png` / `icon.svg` | Your app icon (optional). PNG is used when both exist. |
| `tray.png` / `tray.svg` | Your tray icon (optional). Defaults to your app icon. |
| `icon-small.png` / `icon-small.svg` | Simpler artwork for the 16/20/24 px frames of `icon.ico` (optional). |
| `default/icon.svg` | Built-in app icon: dark tile, blue switch, USB mark. Also `ui/logo.png`. |
| `default/icon-small.svg` | Built-in small artwork (no USB mark and no "on" bar). |
| `default/tray.svg` | Built-in tray glyph (the switch without the tile). |

`set-icons.bat` writes `../icon.ico` (16–256 px frames), the other files in `..`
(PNG set, `icon.icns`, Store logos), `../tray/<size>.png` and `../../../ui/logo.png`.
It runs `tools/set-icons.ps1`, which uses the Tauri CLI to rasterize the images.
