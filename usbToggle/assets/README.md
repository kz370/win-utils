# Icons

| File | Where it shows |
| --- | --- |
| `app_icon.ico` | Window title bar, **taskbar**, Start menu, the `.exe` file, the installer |
| `tray_icon.ico` | The tray icon next to the clock |
| `header_logo.png` | The logo at the top inside the app window |

To use your own icons, either:

- run `set-icons.bat my-icon.png` in the `usbToggle` folder (or drag the image onto it). It makes all
  three files from one square PNG or SVG, or use `set-icons.bat app.png tray.png` for a separate tray image.
  `set-icons.bat --reset` goes back to the built-in icons; or
- replace the files here with your own, **keeping the names**. The `.ico` files should contain several
  sizes: 16, 24, 32, 48, 256 px for the app icon and 16, 20, 24, 32 px or more for the tray (the tray
  picks the size that fits your screen scaling). `header_logo.png` should be square, 256 px or larger.

Then run `build.bat`. If Windows still shows the old icon on the taskbar or desktop, unpin and re-pin
the app; Windows caches icons.

`source/` holds the editable artwork of the built-in icons (`app_icon.svg`, `app_icon_small.svg` for the
16/20/24 px frames, `tray_icon.svg`); `set-icons.bat --reset` rebuilds from it.
