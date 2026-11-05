//! Small Win32 helpers shared by the Windows-only modules.

pub fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Reads a NUL-terminated (or full-length) UTF-16 buffer.
pub fn wide_to_string(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end]).trim().to_string()
}

#[cfg(windows)]
pub fn last_error() -> u32 {
    unsafe { windows_sys::Win32::Foundation::GetLastError() }
}

/// Runs a console tool without flashing a console window.
#[cfg(windows)]
pub fn hidden_command(program: &str) -> std::process::Command {
    use std::os::windows::process::CommandExt;
    let mut cmd = std::process::Command::new(program);
    cmd.creation_flags(windows_sys::Win32::System::Threading::CREATE_NO_WINDOW);
    cmd
}
