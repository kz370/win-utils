//! "Start with Windows".
//!
//! When the app runs as administrator it registers a logon task with highest
//! privileges, so it starts elevated without a UAC prompt. Otherwise it falls
//! back to the per-user Run key (the app then starts without admin rights).

use serde::Serialize;

pub const TASK_NAME: &str = "usbToggle";
const RUN_KEY: &str = r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run";
const RUN_VALUE: &str = "usbToggle";
pub const AUTOSTART_ARG: &str = "--autostart";

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AutostartState {
    pub enabled: bool,
    /// True when it starts with administrator rights (scheduled task).
    pub elevated: bool,
}

#[cfg(windows)]
mod imp {
    use super::*;
    use crate::win::hidden_command;

    fn task_exists() -> bool {
        hidden_command("schtasks.exe")
            .args(["/Query", "/TN", TASK_NAME])
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false)
    }

    fn run_value_exists() -> bool {
        hidden_command("reg.exe")
            .args(["query", RUN_KEY, "/v", RUN_VALUE])
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false)
    }

    pub fn state() -> AutostartState {
        let task = task_exists();
        AutostartState { enabled: task || run_value_exists(), elevated: task }
    }

    fn xml_escape(s: &str) -> String {
        s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
    }

    fn create_task(exe: &str) -> Result<(), String> {
        let user = match (std::env::var("USERDOMAIN"), std::env::var("USERNAME")) {
            (Ok(d), Ok(u)) => format!("{d}\\{u}"),
            (_, Ok(u)) => u,
            _ => return Err("Couldn't determine the current user.".into()),
        };
        let xml = format!(
            r#"<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Starts USB Toggle in the tray when you sign in.</Description></RegistrationInfo>
  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>{user}</UserId><Delay>PT3S</Delay></LogonTrigger></Triggers>
  <Principals><Principal id="Author"><UserId>{user}</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>7</Priority>
  </Settings>
  <Actions Context="Author"><Exec><Command>{exe}</Command><Arguments>{arg}</Arguments></Exec></Actions>
</Task>"#,
            user = xml_escape(&user),
            exe = xml_escape(exe),
            arg = AUTOSTART_ARG,
        );
        let path = std::env::temp_dir().join(format!("usbToggle-task-{}.xml", std::process::id()));
        // Task Scheduler expects UTF-16 LE with a BOM for this declaration.
        let mut bytes = vec![0xFF, 0xFE];
        for unit in xml.encode_utf16() {
            bytes.extend_from_slice(&unit.to_le_bytes());
        }
        std::fs::write(&path, bytes).map_err(|e| e.to_string())?;
        let out = hidden_command("schtasks.exe")
            .args(["/Create", "/TN", TASK_NAME, "/F", "/XML"])
            .arg(&path)
            .output();
        let _ = std::fs::remove_file(&path);
        let out = out.map_err(|e| e.to_string())?;
        if out.status.success() {
            Ok(())
        } else {
            Err(format!("Couldn't create the startup task: {}", String::from_utf8_lossy(&out.stderr).trim()))
        }
    }

    fn delete_task() -> Result<(), String> {
        if !task_exists() {
            return Ok(());
        }
        let out = hidden_command("schtasks.exe")
            .args(["/Delete", "/TN", TASK_NAME, "/F"])
            .output()
            .map_err(|e| e.to_string())?;
        if out.status.success() {
            Ok(())
        } else {
            Err("Couldn't remove the startup task. Run USB Toggle as administrator to change this.".into())
        }
    }

    fn set_run_value(exe: Option<&str>) -> Result<(), String> {
        let out = match exe {
            Some(exe) => hidden_command("reg.exe")
                .args(["add", RUN_KEY, "/v", RUN_VALUE, "/t", "REG_SZ", "/f", "/d"])
                .arg(format!("\"{exe}\" {AUTOSTART_ARG}"))
                .output(),
            None => {
                if !run_value_exists() {
                    return Ok(());
                }
                hidden_command("reg.exe").args(["delete", RUN_KEY, "/v", RUN_VALUE, "/f"]).output()
            }
        }
        .map_err(|e| e.to_string())?;
        if out.status.success() {
            Ok(())
        } else {
            Err(format!("Couldn't update the startup entry: {}", String::from_utf8_lossy(&out.stderr).trim()))
        }
    }

    pub fn set(enabled: bool, elevated: bool) -> Result<AutostartState, String> {
        let exe = std::env::current_exe().map_err(|e| e.to_string())?;
        let exe = exe.to_string_lossy().to_string();
        if enabled {
            if elevated {
                create_task(&exe)?;
                set_run_value(None)?;
            } else {
                set_run_value(Some(&exe))?;
            }
        } else {
            set_run_value(None)?;
            delete_task()?;
        }
        Ok(state())
    }
}

#[cfg(windows)]
pub use imp::{set, state};

#[cfg(not(windows))]
pub fn state() -> AutostartState {
    AutostartState { enabled: false, elevated: false }
}

#[cfg(not(windows))]
pub fn set(_enabled: bool, _elevated: bool) -> Result<AutostartState, String> {
    Err("Only supported on Windows.".into())
}
