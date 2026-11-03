use std::process::Command;

const POWERSHELL: &str = "powershell.exe";
const DEFAULT_TIMEOUT_MS: u64 = 15000;

#[derive(Debug, Clone, PartialEq, serde::Serialize)]
pub enum DeviceActionError {
    None,
    Success,
    AccessDenied,
    DeviceMissing,
    Timeout,
    Ambiguous,
    Other,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct DeviceActionResult {
    pub instance_id: String,
    pub success: bool,
    pub error: DeviceActionError,
    pub error_message: Option<String>,
    pub milliseconds: u64,
}

#[derive(Debug, serde::Deserialize)]
struct PsOutput {
    success: bool,
    error: Option<String>,
    code: Option<String>,
}

pub struct PnpDeviceController;

impl PnpDeviceController {
    pub fn enable(instance_id: &str) -> DeviceActionResult {
        Self::run("Enable", ENABLE_SCRIPT, instance_id)
    }

    pub fn disable(instance_id: &str) -> DeviceActionResult {
        Self::run("Disable", DISABLE_SCRIPT, instance_id)
    }

    fn run(action: &str, script: &str, instance_id: &str) -> DeviceActionResult {
        let mut result = DeviceActionResult {
            instance_id: instance_id.to_string(),
            success: false,
            error: DeviceActionError::Other,
            error_message: None,
            milliseconds: 0,
        };

        let ps1_path = std::env::temp_dir().join(format!("usbToggle-{}-{}.ps1", action, uuid::Uuid::new_v4()));
        if let Err(ex) = std::fs::write(&ps1_path, script) {
            result.error_message = Some(format!("Failed to write PowerShell script: {ex}"));
            return result;
        }

        let start = std::time::Instant::now();
        let mut child = match Command::new(POWERSHELL)
            .arg("-NoProfile")
            .arg("-NonInteractive")
            .arg("-ExecutionPolicy")
            .arg("Bypass")
            .arg("-File")
            .arg(&ps1_path)
            .arg("-InstanceId")
            .arg(instance_id)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .spawn()
        {
            Ok(c) => c,
            Err(ex) => {
                result.milliseconds = start.elapsed().as_millis() as u64;
                result.error_message = Some(format!("PowerShell process failed to start: {ex}"));
                let _ = std::fs::remove_file(&ps1_path);
                return result;
            }
        };

        result.milliseconds = start.elapsed().as_millis() as u64;

        let output = match child.wait_with_output() {
            Ok(o) => o,
            Err(ex) => {
                result.error_message = Some(format!("PowerShell {action} failed: {ex}"));
                let _ = std::fs::remove_file(&ps1_path);
                return result;
            }
        };
        let _ = std::fs::remove_file(&ps1_path);

        let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();

        if !output.status.success() {
            result.error = DeviceActionError::Other;
            result.error_message = Some(if stdout.is_empty() {
                format!("PowerShell exited {}. {}", output.status.code().map(|c| c.to_string()).unwrap_or_default(), stderr)
            } else {
                stdout.clone()
            });
            return result;
        }

        match serde_json::from_str::<PsOutput>(&stdout) {
            Ok(ps) => {
                result.success = ps.success;
                result.error = match ps.code.as_deref() {
                    Some("Success") => DeviceActionError::Success,
                    Some("AccessDenied") => DeviceActionError::AccessDenied,
                    Some("DeviceMissing") => DeviceActionError::DeviceMissing,
                    _ => DeviceActionError::Other,
                };
                result.error_message = ps.error;
            }
            Err(_) => {
                result.success = false;
                result.error = DeviceActionError::Other;
                result.error_message = Some(format!("Unparseable PowerShell output: {stdout} {stderr}"));
            }
        }

        result
    }
}

const ENABLE_SCRIPT: &str = r#"
param([string]$InstanceId)
if (-not $InstanceId) { Write-Output (ConvertTo-Json @{ success = $false; error = 'missing-instance-id'; code = 'Other' }); exit 0 }
try {
    Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction Stop
    Write-Output (ConvertTo-Json @{ success = $true; error = $null; code = 'Success' })
} catch {
    $msg = $_.Exception.Message
    $code = 'Other'
    if ($msg -like '*access denied*') { $code = 'AccessDenied' }
    elseif ($msg -like '*not found*' -or $msg -like '*cannot find*') { $code = 'DeviceMissing' }
    Write-Output (ConvertTo-Json @{ success = $false; error = $msg; code = $code })
    exit 0
}
"#;

const DISABLE_SCRIPT: &str = r#"
param([string]$InstanceId)
if (-not $InstanceId) { Write-Output (ConvertTo-Json @{ success = $false; error = 'missing-instance-id'; code = 'Other' }); exit 0 }
try {
    Disable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction Stop
    Write-Output (ConvertTo-Json @{ success = $true; error = $null; code = 'Success' })
} catch {
    $msg = $_.Exception.Message
    $code = 'Other'
    if ($msg -like '*access denied*') { $code = 'AccessDenied' }
    elseif ($msg -like '*not found*' -or $msg -like '*cannot find*') { $code = 'DeviceMissing' }
    Write-Output (ConvertTo-Json @{ success = $false; error = $msg; code = $code })
    exit 0
}
"#;
