//! Administrator detection and "restart as administrator".

#[cfg(windows)]
pub fn is_elevated() -> bool {
    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE};
    use windows_sys::Win32::Security::{GetTokenInformation, TokenElevation, TOKEN_ELEVATION, TOKEN_QUERY};
    use windows_sys::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};
    unsafe {
        let mut token: HANDLE = std::ptr::null_mut();
        if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) == 0 {
            return false;
        }
        let mut elevation = TOKEN_ELEVATION { TokenIsElevated: 0 };
        let mut len = 0u32;
        let ok = GetTokenInformation(
            token,
            TokenElevation,
            (&mut elevation as *mut TOKEN_ELEVATION).cast(),
            std::mem::size_of::<TOKEN_ELEVATION>() as u32,
            &mut len,
        );
        CloseHandle(token);
        ok != 0 && elevation.TokenIsElevated != 0
    }
}

#[cfg(not(windows))]
pub fn is_elevated() -> bool {
    false
}

/// Starts a new elevated copy of the app. Returns Ok once the user accepted
/// the UAC prompt; the caller should then exit.
#[cfg(windows)]
pub fn relaunch_elevated(extra_args: &str) -> Result<(), String> {
    use crate::win::wide;
    use windows_sys::Win32::UI::Shell::ShellExecuteW;
    use windows_sys::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;

    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let args = format!("--wait-for-pid {} {}", std::process::id(), extra_args);
    let verb = wide("runas");
    let file = wide(&exe.to_string_lossy());
    let params = wide(args.trim());
    let result = unsafe {
        ShellExecuteW(std::ptr::null_mut(), verb.as_ptr(), file.as_ptr(), params.as_ptr(), std::ptr::null(), SW_SHOWNORMAL)
    };
    // ShellExecute returns a value > 32 on success.
    if result as isize > 32 {
        Ok(())
    } else {
        Err("Administrator permission was not granted.".into())
    }
}

#[cfg(not(windows))]
pub fn relaunch_elevated(_extra_args: &str) -> Result<(), String> {
    Err("Only supported on Windows.".into())
}

/// When started by `relaunch_elevated`, wait for the old instance to exit so
/// the single-instance guard doesn't hand control back to it.
pub fn wait_for_previous_instance() {
    let args: Vec<String> = std::env::args().collect();
    let Some(pos) = args.iter().position(|a| a == "--wait-for-pid") else { return };
    let Some(pid) = args.get(pos + 1).and_then(|p| p.parse::<u32>().ok()) else { return };
    #[cfg(windows)]
    unsafe {
        use windows_sys::Win32::Foundation::CloseHandle;
        use windows_sys::Win32::System::Threading::{OpenProcess, WaitForSingleObject, PROCESS_SYNCHRONIZE};
        let handle = OpenProcess(PROCESS_SYNCHRONIZE, 0, pid);
        if !handle.is_null() {
            WaitForSingleObject(handle, 5000);
            CloseHandle(handle);
        }
    }
    #[cfg(not(windows))]
    let _ = pid;
}
