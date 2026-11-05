Source artwork for the app icons.

- `icon.svg` — the app icon (32 px and up).
- `icon-small.svg` — same icon without the USB mark and "on" bar, used for the
  16/20/24 px frames of `icon.ico`, where those details turn into noise.
- `tray.svg` — the tray glyph (the switch without the tile), pre-rendered to
  `../tray/<size>.png`; the app picks the size for the screen's DPI.

To regenerate: render `icon.svg` to a 1024 px PNG and run
`npx tauri icon icon-1024.png -o src-tauri/icons`, then rebuild `icon.ico` so its
16–24 px frames come from `icon-small.svg`.
