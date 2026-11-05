# RUNTIME

Tracking file for the usbToggle documentation run. Paths are relative to `usbToggle/`.

## Files to Process

- .gitignore
- README.md
- build.bat
- installer/portable.txt
- installer/usbToggle.iss
- package.json
- src-tauri/.gitignore
- src-tauri/Cargo.lock
- src-tauri/Cargo.toml
- src-tauri/build.rs
- src-tauri/capabilities/default.json
- src-tauri/icons/128x128.png
- src-tauri/icons/128x128@2x.png
- src-tauri/icons/32x32.png
- src-tauri/icons/Square107x107Logo.png
- src-tauri/icons/Square142x142Logo.png
- src-tauri/icons/Square150x150Logo.png
- src-tauri/icons/Square284x284Logo.png
- src-tauri/icons/Square30x30Logo.png
- src-tauri/icons/Square310x310Logo.png
- src-tauri/icons/Square44x44Logo.png
- src-tauri/icons/Square71x71Logo.png
- src-tauri/icons/Square89x89Logo.png
- src-tauri/icons/StoreLogo.png
- src-tauri/icons/icon.icns
- src-tauri/icons/icon.ico
- src-tauri/icons/icon.png
- src-tauri/icons/source/README.md
- src-tauri/icons/source/icon-small.svg
- src-tauri/icons/source/icon.svg
- src-tauri/icons/source/tray.svg
- src-tauri/icons/tray/16.png
- src-tauri/icons/tray/20.png
- src-tauri/icons/tray/24.png
- src-tauri/icons/tray/32.png
- src-tauri/icons/tray/40.png
- src-tauri/icons/tray/48.png
- src-tauri/icons/tray/64.png
- src-tauri/src/autostart.rs
- src-tauri/src/classify.rs
- src-tauri/src/config.rs
- src-tauri/src/control.rs
- src-tauri/src/devices.rs
- src-tauri/src/elevation.rs
- src-tauri/src/lib.rs
- src-tauri/src/logger.rs
- src-tauri/src/main.rs
- src-tauri/src/notify.rs
- src-tauri/src/paths.rs
- src-tauri/src/win.rs
- src-tauri/tauri.conf.json
- ui/app.js
- ui/icons.js
- ui/index.html
- ui/logo.svg
- ui/styles.css

## Files Processed

- .gitignore
- README.md
- build.bat
- installer/portable.txt
- installer/usbToggle.iss
- package.json
- src-tauri/.gitignore
- src-tauri/Cargo.lock
- src-tauri/Cargo.toml
- src-tauri/build.rs
- src-tauri/capabilities/default.json
- src-tauri/icons/128x128.png
- src-tauri/icons/128x128@2x.png
- src-tauri/icons/32x32.png
- src-tauri/icons/Square107x107Logo.png
- src-tauri/icons/Square142x142Logo.png
- src-tauri/icons/Square150x150Logo.png
- src-tauri/icons/Square284x284Logo.png
- src-tauri/icons/Square30x30Logo.png
- src-tauri/icons/Square310x310Logo.png
- src-tauri/icons/Square44x44Logo.png
- src-tauri/icons/Square71x71Logo.png
- src-tauri/icons/Square89x89Logo.png
- src-tauri/icons/StoreLogo.png
- src-tauri/icons/icon.icns
- src-tauri/icons/icon.ico
- src-tauri/icons/icon.png
- src-tauri/icons/source/README.md
- src-tauri/icons/source/icon-small.svg
- src-tauri/icons/source/icon.svg
- src-tauri/icons/source/tray.svg
- src-tauri/icons/tray/16.png
- src-tauri/icons/tray/20.png
- src-tauri/icons/tray/24.png
- src-tauri/icons/tray/32.png
- src-tauri/icons/tray/40.png
- src-tauri/icons/tray/48.png
- src-tauri/icons/tray/64.png
- src-tauri/src/autostart.rs
- src-tauri/src/classify.rs
- src-tauri/src/config.rs
- src-tauri/src/control.rs
- src-tauri/src/devices.rs
- src-tauri/src/elevation.rs
- src-tauri/src/lib.rs
- src-tauri/src/logger.rs
- src-tauri/src/main.rs
- src-tauri/src/notify.rs
- src-tauri/src/paths.rs
- src-tauri/src/win.rs
- src-tauri/tauri.conf.json
- ui/app.js
- ui/icons.js
- ui/index.html
- ui/logo.svg
- ui/styles.css

## Log

- [2026-11-05T03:26:23Z] Initialization completed
- [2026-11-05T03:26:23Z] File discovery completed (56 tracked files; untracked build output such as node_modules/, src-tauri/target/ and src-tauri/gen/ excluded)
- [2026-11-05T03:32:40Z] Architectural analysis completed (specs/architecture_overview.md)
- [2026-11-05T03:32:40Z] File-by-file analysis completed: 56/56 files written to specs/full_documentation.md (section 9)
- [2026-11-05T03:32:40Z] Sections 1-8 of specs/full_documentation.md completed (introduction, architecture, modules, business rules, data models, flows, diagram references, known limitations)
- [2026-11-05T03:45:35Z] specs/api_reference.md completed (12 commands, 3 events, CLI arguments, shared types)
- [2026-11-05T03:45:35Z] specs/developer_guide.md completed (setup, dependencies, run, tests, debugging, extension, style, contribution)
- [2026-11-05T03:45:35Z] specs/catalog.txt, specs/relationships.txt, specs/data_models.txt completed
- [2026-11-05T03:45:35Z] Code changed during the run (commits a2bf11f "hide devices that aren't working" and 23b65a9 "protect devices; fix Bluetooth adapters"); all specs updated to match
- [2026-11-05T03:45:35Z] Diagrams completed: architecture.mmd, dataflow.mmd, classes.mmd, sequence.mmd
- [2026-11-05T03:45:35Z] Verification: diagrams parsed and rendered with Mermaid 11 (4/4 OK)
- [2026-11-05T03:45:35Z] Verification: coverage check passed - 56 "## File:" sections in full_documentation.md, identical to the 56 tracked files
- [2026-11-05T03:45:35Z] Verification: consistency check passed - every Rust function and every app.js function is in catalog.txt; 12/12 IPC commands and 3/3 events are in api_reference.md; every Settings field and Device field is in api_reference.md and data_models.txt
- [2026-11-05T03:45:35Z] Verification: English review completed (no placeholders, no duplicated words)

## Verification Summary

| Check | Result |
| --- | --- |
| Files to Process == Files Processed | 56 == 56 |
| File sections in full_documentation.md | 56, one per tracked file, same paths |
| IPC commands documented | 12 / 12 |
| Events documented | 3 / 3 |
| Rust functions in catalog | all |
| Front-end functions in catalog | all |
| Mermaid diagrams valid | 4 / 4 |

Note: the files in specs/ are the output of this run and are not part of the processed set.

Status: Completed 100%

Documentation ready in /specs (usbToggle/specs/)

