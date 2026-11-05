//! Calls back when devices are plugged in, removed, started or stopped.

use std::sync::mpsc::{channel, Receiver, RecvTimeoutError, Sender};
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

static SENDER: OnceLock<Mutex<Sender<()>>> = OnceLock::new();

/// Starts watching for device changes. `on_change` runs on a background
/// thread, at most once per burst of events.
pub fn watch(on_change: impl Fn() + Send + 'static) {
    let (tx, rx) = channel::<()>();
    let _ = SENDER.set(Mutex::new(tx));

    let registered = register();
    if !registered {
        crate::log_error!("device notifications unavailable; falling back to polling");
    }

    std::thread::spawn(move || debounce_loop(rx, registered, on_change));
}

fn debounce_loop(rx: Receiver<()>, registered: bool, on_change: impl Fn()) {
    let poll = Duration::from_secs(4);
    loop {
        if registered {
            if rx.recv().is_err() {
                return;
            }
        } else {
            match rx.recv_timeout(poll) {
                Ok(()) | Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => return,
            }
        }
        // Device changes arrive in bursts (one per interface/driver); wait
        // for quiet before rescanning.
        loop {
            match rx.recv_timeout(Duration::from_millis(350)) {
                Ok(()) => continue,
                Err(RecvTimeoutError::Timeout) => break,
                Err(RecvTimeoutError::Disconnected) => return,
            }
        }
        on_change();
    }
}

#[cfg(windows)]
fn register() -> bool {
    use windows_sys::Win32::Devices::DeviceAndDriverInstallation::*;

    unsafe extern "system" fn callback(
        _h: HCMNOTIFICATION,
        _ctx: *const core::ffi::c_void,
        _action: CM_NOTIFY_ACTION,
        _data: *const CM_NOTIFY_EVENT_DATA,
        _size: u32,
    ) -> u32 {
        if let Some(tx) = SENDER.get() {
            if let Ok(tx) = tx.lock() {
                let _ = tx.send(());
            }
        }
        0 // ERROR_SUCCESS
    }

    let filter = CM_NOTIFY_FILTER {
        cbSize: std::mem::size_of::<CM_NOTIFY_FILTER>() as u32,
        Flags: CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES,
        FilterType: CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE,
        ..Default::default()
    };
    let mut handle: HCMNOTIFICATION = std::ptr::null_mut();
    let cr = unsafe { CM_Register_Notification(&filter, std::ptr::null(), Some(callback), &mut handle) };
    // The registration lives for the whole process; never unregistered.
    cr == CR_SUCCESS
}

#[cfg(not(windows))]
fn register() -> bool {
    false
}
