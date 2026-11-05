//! Enabling and disabling devices.
//!
//! Connected devices go through the class installer with DIF_PROPERTYCHANGE,
//! exactly like Device Manager does. Disconnected devices can't be restarted,
//! so their persistent "disabled" config flag is flipped instead; Windows
//! honours it the next time the device is plugged in.

use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActionError {
    /// accessDenied | notDisableable | missing | failed
    pub code: &'static str,
    pub message: String,
}

impl ActionError {
    pub fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self { code, message: message.into() }
    }
}

#[derive(Debug, Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActionOutcome {
    pub needs_restart: bool,
}

#[cfg(windows)]
pub use imp::set_enabled;

#[cfg(not(windows))]
pub fn set_enabled(_id: &str, _enable: bool) -> Result<ActionOutcome, ActionError> {
    Err(ActionError::new("failed", "Only supported on Windows."))
}

#[cfg(windows)]
mod imp {
    use super::*;
    use crate::devices::imp::{devnode_status, registry_dword, DevInfoSet};
    use crate::win::{last_error, wide};
    use std::ptr;
    use windows_sys::Win32::Devices::DeviceAndDriverInstallation::*;
    use windows_sys::Win32::Foundation::{ERROR_ACCESS_DENIED, INVALID_HANDLE_VALUE};

    const ERROR_NOT_DISABLEABLE: u32 = 0xE000_0231;
    const ERROR_NO_SUCH_DEVINST: u32 = 0xE000_020B;
    const ERROR_IN_WOW64: u32 = 0xE000_0235;

    fn map_error(code: u32, enable: bool) -> ActionError {
        let verb = if enable { "turn on" } else { "turn off" };
        match code {
            ERROR_ACCESS_DENIED => ActionError::new(
                "accessDenied",
                "Windows needs administrator rights to change devices. Restart USB Toggle as administrator.",
            ),
            ERROR_NOT_DISABLEABLE => ActionError::new("notDisableable", "Windows doesn't allow this device to be turned off."),
            ERROR_NO_SUCH_DEVINST => ActionError::new("missing", "The device is no longer known to Windows. Refresh and try again."),
            ERROR_IN_WOW64 => ActionError::new("failed", "Use the 64-bit build of USB Toggle on 64-bit Windows."),
            other => {
                let detail = if other & 0xE000_0000 == 0xE000_0000 {
                    format!("SetupAPI error 0x{other:08X}")
                } else {
                    std::io::Error::from_raw_os_error(other as i32).to_string()
                };
                ActionError::new("failed", format!("Couldn't {verb} the device: {detail}"))
            }
        }
    }

    pub fn set_enabled(instance_id: &str, enable: bool) -> Result<ActionOutcome, ActionError> {
        let raw = unsafe { SetupDiCreateDeviceInfoList(ptr::null(), ptr::null_mut()) };
        if raw == INVALID_HANDLE_VALUE as HDEVINFO {
            return Err(map_error(last_error(), enable));
        }
        let set = DevInfoSet(raw);

        let mut data = SP_DEVINFO_DATA { cbSize: std::mem::size_of::<SP_DEVINFO_DATA>() as u32, ..Default::default() };
        let id = wide(instance_id);
        if unsafe { SetupDiOpenDeviceInfoW(set.0, id.as_ptr(), ptr::null_mut(), 0, &mut data) } == 0 {
            return Err(map_error(last_error(), enable));
        }

        if devnode_status(data.DevInst).is_some() {
            change_live(&set, &data, enable)
        } else {
            change_offline(&set, &mut data, enable)
        }
    }

    fn call_property_change(set: &DevInfoSet, data: &SP_DEVINFO_DATA, state: u32, scope: u32) -> Result<(), u32> {
        let params = SP_PROPCHANGE_PARAMS {
            ClassInstallHeader: SP_CLASSINSTALL_HEADER {
                cbSize: std::mem::size_of::<SP_CLASSINSTALL_HEADER>() as u32,
                InstallFunction: DIF_PROPERTYCHANGE,
            },
            StateChange: state,
            Scope: scope,
            HwProfile: 0,
        };
        unsafe {
            if SetupDiSetClassInstallParamsW(
                set.0,
                data,
                &params.ClassInstallHeader,
                std::mem::size_of::<SP_PROPCHANGE_PARAMS>() as u32,
            ) == 0
            {
                return Err(last_error());
            }
            if SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set.0, data) == 0 {
                return Err(last_error());
            }
        }
        Ok(())
    }

    fn change_live(set: &DevInfoSet, data: &SP_DEVINFO_DATA, enable: bool) -> Result<ActionOutcome, ActionError> {
        if enable {
            call_property_change(set, data, DICS_ENABLE, DICS_FLAG_GLOBAL).map_err(|e| map_error(e, true))?;
            // Also clear a per-profile disable (left behind by tools like devcon).
            let _ = call_property_change(set, data, DICS_ENABLE, DICS_FLAG_CONFIGSPECIFIC);
        } else {
            call_property_change(set, data, DICS_DISABLE, DICS_FLAG_GLOBAL).map_err(|e| map_error(e, false))?;
        }

        let mut params = SP_DEVINSTALL_PARAMS_W {
            cbSize: std::mem::size_of::<SP_DEVINSTALL_PARAMS_W>() as u32,
            ..unsafe { std::mem::zeroed() }
        };
        let needs_restart = unsafe { SetupDiGetDeviceInstallParamsW(set.0, data, &mut params) } != 0
            && params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART) != 0;
        Ok(ActionOutcome { needs_restart })
    }

    fn change_offline(set: &DevInfoSet, data: &mut SP_DEVINFO_DATA, enable: bool) -> Result<ActionOutcome, ActionError> {
        let flags = registry_dword(set, data, SPDRP_CONFIGFLAGS).unwrap_or(0);
        let new_flags = if enable { flags & !CONFIGFLAG_DISABLED } else { flags | CONFIGFLAG_DISABLED };
        if new_flags != flags {
            let bytes = new_flags.to_le_bytes();
            let ok = unsafe { SetupDiSetDeviceRegistryPropertyW(set.0, data, SPDRP_CONFIGFLAGS, bytes.as_ptr(), 4) };
            if ok == 0 {
                return Err(map_error(last_error(), enable));
            }
        }
        Ok(ActionOutcome { needs_restart: false })
    }
}
