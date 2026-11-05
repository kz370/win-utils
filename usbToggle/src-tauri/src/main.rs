#![allow(non_snake_case)]
// Prevents an extra console window on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    usb_toggle_lib::run();
}
