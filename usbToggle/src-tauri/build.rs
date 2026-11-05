use std::fs;
use std::path::Path;

fn main() {
    // The header logo lives with the other icons in assets/; the window
    // serves ui/, so copy it over (only when it changed).
    let from = Path::new("../assets/header_logo.png");
    let to = Path::new("../ui/logo.png");
    println!("cargo:rerun-if-changed=../assets/header_logo.png");
    println!("cargo:rerun-if-changed=../assets/app_icon.ico");
    println!("cargo:rerun-if-changed=../assets/tray_icon.ico");
    match fs::read(from) {
        Ok(new) => {
            if fs::read(to).map(|old| old != new).unwrap_or(true) {
                if let Err(e) = fs::write(to, new) {
                    println!("cargo:warning=could not copy header_logo.png to ui/logo.png: {e}");
                }
            }
        }
        Err(e) => println!("cargo:warning=assets/header_logo.png is missing ({e}); the window header will have no logo"),
    }

    tauri_build::build()
}
