//! USB device discovery.
//!
//! Lists every device node enumerated by the USB bus driver (enumerator `USB`),
//! both connected and remembered-but-disconnected, and reduces them to the
//! physical devices a person would recognise: interface nodes (`&MI_xx`) and
//! root hubs are folded away, and the drivers loaded *under* each device are
//! used to work out what kind of device it is and what to call it.

use serde::Serialize;
use std::collections::{HashMap, HashSet};

use crate::classify::{self, Category, NodeInfo};

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Device {
    /// Windows device instance ID. Stable for a device on a given port (or
    /// everywhere, when the device reports a serial number). Windows keeps the
    /// enabled/disabled state per instance ID, so this is also our key.
    pub id: String,
    /// Best automatic name (custom names are applied later).
    pub default_name: String,
    pub name: String,
    pub custom_name: Option<String>,
    pub description: String,
    pub vendor: Option<String>,
    pub vid: Option<String>,
    pub pid: Option<String>,
    pub category: Category,
    pub present: bool,
    pub enabled: bool,
    /// CM problem code for a connected device (0 = working).
    pub problem: u32,
    pub is_hub: bool,
    /// Instance ID of the external hub this device is plugged into, if any.
    pub hub_id: Option<String>,
    pub hub_name: Option<String>,
    pub port: Option<u32>,
    /// Last time Windows saw the device arrive or leave (Unix ms).
    pub last_seen: Option<i64>,
    /// Names of the functions/drivers under this device (keyboard, disk, ...).
    pub functions: Vec<String>,
    pub pinned: bool,
}

#[cfg(windows)]
pub use imp::scan;

#[cfg(not(windows))]
pub fn scan() -> Result<Vec<Device>, String> {
    Err("USB device control is only available on Windows.".into())
}

/// Turns the raw nodes into devices. Separate from the Win32 code so the
/// grouping rules are plain data transformations.
#[allow(dead_code)]
pub(crate) fn assemble(nodes: Vec<RawNode>) -> Vec<Device> {
    let by_id: HashMap<String, usize> = nodes
        .iter()
        .enumerate()
        .map(|(i, n)| (n.instance_id.clone(), i))
        .collect();

    // Interface nodes of disconnected composite devices: link them to their
    // parent when only one device has that VID/PID, so categories still work.
    let mut top_by_vidpid: HashMap<String, Vec<usize>> = HashMap::new();
    for (i, n) in nodes.iter().enumerate() {
        if is_top_level(n) {
            if let Some(key) = vidpid_key(&n.instance_id) {
                top_by_vidpid.entry(key).or_default().push(i);
            }
        }
    }
    let mut orphan_functions: HashMap<usize, Vec<NodeInfo>> = HashMap::new();
    for n in nodes.iter().filter(|n| is_interface(n) && !n.present) {
        if let Some(key) = vidpid_key(&n.instance_id) {
            if let Some(owners) = top_by_vidpid.get(&key) {
                if owners.len() == 1 {
                    orphan_functions.entry(owners[0]).or_default().push(n.info.clone());
                }
            }
        }
    }

    let mut out = Vec::new();
    for (i, n) in nodes.iter().enumerate() {
        if !is_top_level(n) {
            continue;
        }
        let mut functions: Vec<NodeInfo> = n.descendants.clone();
        if let Some(extra) = orphan_functions.get(&i) {
            functions.extend(extra.iter().cloned());
        }

        let is_hub = classify::is_hub(&n.info);
        let category = if is_hub {
            Category::Hub
        } else {
            classify::categorize(&n.info, &functions)
        };
        let (vid, pid) = classify::parse_vid_pid(&n.instance_id);
        let vendor = vid.as_deref().and_then(classify::vendor_name).map(str::to_string);
        let default_name = classify::display_name(&n.info, &functions, category, vendor.as_deref());

        let hub = n
            .parent_id
            .as_ref()
            .and_then(|p| by_id.get(p))
            .map(|&pi| &nodes[pi])
            .filter(|p| classify::is_hub(&p.info) && !is_root_hub(p));

        let mut function_names: Vec<String> = Vec::new();
        let mut seen = HashSet::new();
        // A hub's descendants are the devices plugged into it, not its functions.
        for f in functions.iter().filter(|_| !is_hub) {
            if let Some(name) = classify::function_label(f) {
                if seen.insert(name.to_lowercase()) {
                    function_names.push(name);
                }
            }
        }

        out.push(Device {
            id: n.instance_id.clone(),
            name: default_name.clone(),
            default_name,
            custom_name: None,
            description: n.info.description.clone(),
            vendor,
            vid,
            pid,
            category,
            present: n.present,
            enabled: !n.disabled,
            problem: if n.present { n.problem } else { 0 },
            is_hub,
            hub_id: hub.map(|h| h.instance_id.clone()),
            hub_name: hub.map(|h| {
                classify::display_name(&h.info, &h.descendants, Category::Hub, None)
            }),
            port: classify::parse_port(&n.location),
            last_seen: n.last_seen,
            functions: function_names,
            pinned: false,
        });
    }
    out
}

#[derive(Debug, Clone, Default)]
pub(crate) struct RawNode {
    pub instance_id: String,
    pub info: NodeInfo,
    pub present: bool,
    pub disabled: bool,
    pub problem: u32,
    pub location: String,
    pub parent_id: Option<String>,
    pub last_seen: Option<i64>,
    /// Functions below this node (connected devices only).
    pub descendants: Vec<NodeInfo>,
}

fn device_part(instance_id: &str) -> &str {
    // "USB\VID_046D&PID_C52B&MI_00\7&1A2B..." -> "VID_046D&PID_C52B&MI_00"
    instance_id.split('\\').nth(1).unwrap_or("")
}

fn is_interface(n: &RawNode) -> bool {
    device_part(&n.instance_id).to_ascii_uppercase().contains("&MI_")
}

fn is_root_hub(n: &RawNode) -> bool {
    device_part(&n.instance_id).to_ascii_uppercase().starts_with("ROOT_HUB")
}

fn is_top_level(n: &RawNode) -> bool {
    n.instance_id.to_ascii_uppercase().starts_with("USB\\") && !is_interface(n) && !is_root_hub(n)
}

fn vidpid_key(instance_id: &str) -> Option<String> {
    match classify::parse_vid_pid(instance_id) {
        (Some(v), Some(p)) => Some(format!("{v}:{p}")),
        _ => None,
    }
}

#[cfg(windows)]
pub(crate) mod imp {
    use super::*;
    use crate::win::{wide, wide_to_string};
    use std::ptr;
    use windows_sys::Win32::Devices::DeviceAndDriverInstallation::*;
    use windows_sys::Win32::Devices::Properties::*;
    use windows_sys::Win32::Foundation::{DEVPROPKEY, FILETIME, INVALID_HANDLE_VALUE};

    /// Owns an HDEVINFO and destroys it on drop.
    pub(crate) struct DevInfoSet(pub HDEVINFO);

    impl Drop for DevInfoSet {
        fn drop(&mut self) {
            unsafe {
                SetupDiDestroyDeviceInfoList(self.0);
            }
        }
    }

    pub(crate) fn open_set(flags: u32) -> Result<DevInfoSet, String> {
        let enumerator = wide("USB");
        let h = unsafe { SetupDiGetClassDevsW(ptr::null(), enumerator.as_ptr(), ptr::null_mut(), DIGCF_ALLCLASSES | flags) };
        if h == INVALID_HANDLE_VALUE as HDEVINFO {
            return Err(format!("Could not list USB devices ({}).", std::io::Error::last_os_error()));
        }
        Ok(DevInfoSet(h))
    }

    pub(crate) fn enum_devices(set: &DevInfoSet) -> Vec<SP_DEVINFO_DATA> {
        let mut out = Vec::new();
        let mut index = 0u32;
        loop {
            let mut data = SP_DEVINFO_DATA { cbSize: std::mem::size_of::<SP_DEVINFO_DATA>() as u32, ..Default::default() };
            if unsafe { SetupDiEnumDeviceInfo(set.0, index, &mut data) } == 0 {
                break;
            }
            out.push(data);
            index += 1;
            if index > 20_000 {
                break;
            }
        }
        out
    }

    pub(crate) fn instance_id(set: &DevInfoSet, data: &SP_DEVINFO_DATA) -> String {
        let mut buf = vec![0u16; 512];
        let mut required = 0u32;
        let ok = unsafe { SetupDiGetDeviceInstanceIdW(set.0, data, buf.as_mut_ptr(), buf.len() as u32, &mut required) };
        if ok == 0 {
            if required as usize > buf.len() {
                buf = vec![0u16; required as usize];
                if unsafe { SetupDiGetDeviceInstanceIdW(set.0, data, buf.as_mut_ptr(), buf.len() as u32, &mut required) } == 0 {
                    return String::new();
                }
            } else {
                return String::new();
            }
        }
        wide_to_string(&buf)
    }

    fn registry_bytes(set: &DevInfoSet, data: &SP_DEVINFO_DATA, property: u32) -> Option<Vec<u8>> {
        let mut required = 0u32;
        let mut reg_type = 0u32;
        let mut buf = vec![0u8; 512];
        let ok = unsafe {
            SetupDiGetDeviceRegistryPropertyW(set.0, data, property, &mut reg_type, buf.as_mut_ptr(), buf.len() as u32, &mut required)
        };
        if ok == 0 {
            if required as usize <= buf.len() || required == 0 {
                return None;
            }
            buf = vec![0u8; required as usize];
            let ok = unsafe {
                SetupDiGetDeviceRegistryPropertyW(set.0, data, property, &mut reg_type, buf.as_mut_ptr(), buf.len() as u32, &mut required)
            };
            if ok == 0 {
                return None;
            }
        }
        buf.truncate(required as usize);
        Some(buf)
    }

    fn bytes_to_wide(bytes: &[u8]) -> Vec<u16> {
        bytes.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect()
    }

    fn registry_string(set: &DevInfoSet, data: &SP_DEVINFO_DATA, property: u32) -> String {
        registry_bytes(set, data, property)
            .map(|b| wide_to_string(&bytes_to_wide(&b)))
            .unwrap_or_default()
    }

    fn registry_multi(set: &DevInfoSet, data: &SP_DEVINFO_DATA, property: u32) -> Vec<String> {
        registry_bytes(set, data, property)
            .map(|b| split_multi(&bytes_to_wide(&b)))
            .unwrap_or_default()
    }

    pub(crate) fn registry_dword(set: &DevInfoSet, data: &SP_DEVINFO_DATA, property: u32) -> Option<u32> {
        registry_bytes(set, data, property).and_then(|b| b.get(0..4).map(|s| u32::from_le_bytes([s[0], s[1], s[2], s[3]])))
    }

    fn split_multi(w: &[u16]) -> Vec<String> {
        w.split(|&c| c == 0)
            .filter(|s| !s.is_empty())
            .map(String::from_utf16_lossy)
            .collect()
    }

    fn property_bytes(set: &DevInfoSet, data: &SP_DEVINFO_DATA, key: &DEVPROPKEY) -> Option<(u32, Vec<u8>)> {
        let mut prop_type = 0u32;
        let mut required = 0u32;
        let mut buf = vec![0u8; 512];
        let ok = unsafe {
            SetupDiGetDevicePropertyW(set.0, data, key, &mut prop_type, buf.as_mut_ptr(), buf.len() as u32, &mut required, 0)
        };
        if ok == 0 {
            if required as usize <= buf.len() || required == 0 {
                return None;
            }
            buf = vec![0u8; required as usize];
            let ok = unsafe {
                SetupDiGetDevicePropertyW(set.0, data, key, &mut prop_type, buf.as_mut_ptr(), buf.len() as u32, &mut required, 0)
            };
            if ok == 0 {
                return None;
            }
        }
        buf.truncate(required as usize);
        Some((prop_type, buf))
    }

    fn property_string(set: &DevInfoSet, data: &SP_DEVINFO_DATA, key: &DEVPROPKEY) -> String {
        match property_bytes(set, data, key) {
            Some((t, b)) if t == DEVPROP_TYPE_STRING => wide_to_string(&bytes_to_wide(&b)),
            _ => String::new(),
        }
    }

    fn property_filetime_ms(set: &DevInfoSet, data: &SP_DEVINFO_DATA, key: &DEVPROPKEY) -> Option<i64> {
        match property_bytes(set, data, key) {
            Some((t, b)) if t == DEVPROP_TYPE_FILETIME && b.len() >= 8 => {
                let ft = FILETIME {
                    dwLowDateTime: u32::from_le_bytes([b[0], b[1], b[2], b[3]]),
                    dwHighDateTime: u32::from_le_bytes([b[4], b[5], b[6], b[7]]),
                };
                let ticks = ((ft.dwHighDateTime as u64) << 32) | ft.dwLowDateTime as u64;
                // 100ns ticks since 1601-01-01 -> ms since 1970-01-01.
                let ms = (ticks / 10_000) as i64 - 11_644_473_600_000;
                (ms > 0).then_some(ms)
            }
            _ => None,
        }
    }

    pub(crate) fn devnode_status(dev_inst: u32) -> Option<(u32, u32)> {
        let mut status = 0u32;
        let mut problem = 0u32;
        let cr = unsafe { CM_Get_DevNode_Status(&mut status, &mut problem, dev_inst, 0) };
        (cr == CR_SUCCESS).then_some((status, if status & DN_HAS_PROBLEM != 0 { problem } else { 0 }))
    }

    fn cm_device_id(dev_inst: u32) -> Option<String> {
        let mut buf = vec![0u16; 512];
        let cr = unsafe { CM_Get_Device_IDW(dev_inst, buf.as_mut_ptr(), buf.len() as u32, 0) };
        (cr == CR_SUCCESS).then(|| wide_to_string(&buf))
    }

    fn cm_registry(dev_inst: u32, property: u32) -> Vec<u16> {
        let mut buf = vec![0u16; 512];
        let mut len = (buf.len() * 2) as u32;
        let mut reg_type = 0u32;
        let cr = unsafe { CM_Get_DevNode_Registry_PropertyW(dev_inst, property, &mut reg_type, buf.as_mut_ptr().cast(), &mut len, 0) };
        if cr != CR_SUCCESS {
            return Vec::new();
        }
        buf.truncate((len as usize) / 2);
        buf
    }

    fn cm_node_info(dev_inst: u32) -> NodeInfo {
        NodeInfo {
            class: wide_to_string(&cm_registry(dev_inst, CM_DRP_CLASS)),
            description: wide_to_string(&cm_registry(dev_inst, CM_DRP_DEVICEDESC)),
            friendly_name: wide_to_string(&cm_registry(dev_inst, CM_DRP_FRIENDLYNAME)),
            bus_description: String::new(),
            service: wide_to_string(&cm_registry(dev_inst, CM_DRP_SERVICE)),
            hardware_ids: split_multi(&cm_registry(dev_inst, CM_DRP_HARDWAREID)),
            compatible_ids: split_multi(&cm_registry(dev_inst, CM_DRP_COMPATIBLEIDS)),
        }
    }

    fn cm_parent(dev_inst: u32) -> Option<u32> {
        let mut parent = 0u32;
        (unsafe { CM_Get_Parent(&mut parent, dev_inst, 0) } == CR_SUCCESS && parent != 0).then_some(parent)
    }

    /// Collects every devnode below `dev_inst` (bounded depth/count).
    fn descendants(dev_inst: u32, depth: u32, out: &mut Vec<NodeInfo>) {
        if depth > 4 || out.len() > 64 {
            return;
        }
        let mut child = 0u32;
        if unsafe { CM_Get_Child(&mut child, dev_inst, 0) } != CR_SUCCESS {
            return;
        }
        loop {
            out.push(cm_node_info(child));
            descendants(child, depth + 1, out);
            let mut next = 0u32;
            if unsafe { CM_Get_Sibling(&mut next, child, 0) } != CR_SUCCESS || out.len() > 64 {
                break;
            }
            child = next;
        }
    }

    pub fn scan() -> Result<Vec<Device>, String> {
        // Which instance IDs are connected right now.
        let present_set = open_set(DIGCF_PRESENT)?;
        let present: HashSet<String> = enum_devices(&present_set)
            .iter()
            .map(|d| instance_id(&present_set, d).to_ascii_uppercase())
            .collect();
        drop(present_set);

        let set = open_set(0)?;
        let mut nodes = Vec::new();
        for data in enum_devices(&set) {
            let id = instance_id(&set, &data);
            if id.is_empty() {
                continue;
            }
            let is_present = present.contains(&id.to_ascii_uppercase());
            let info = NodeInfo {
                class: registry_string(&set, &data, SPDRP_CLASS),
                description: registry_string(&set, &data, SPDRP_DEVICEDESC),
                friendly_name: registry_string(&set, &data, SPDRP_FRIENDLYNAME),
                bus_description: property_string(&set, &data, &DEVPKEY_Device_BusReportedDeviceDesc),
                service: registry_string(&set, &data, SPDRP_SERVICE),
                hardware_ids: registry_multi(&set, &data, SPDRP_HARDWAREID),
                compatible_ids: registry_multi(&set, &data, SPDRP_COMPATIBLEIDS),
            };
            let config_flags = registry_dword(&set, &data, SPDRP_CONFIGFLAGS).unwrap_or(0);

            let mut node = RawNode {
                instance_id: id,
                info,
                present: is_present,
                location: registry_string(&set, &data, SPDRP_LOCATION_INFORMATION),
                ..Default::default()
            };

            if is_present {
                let (_, problem) = devnode_status(data.DevInst).unwrap_or((0, 0));
                node.problem = problem;
                node.disabled = problem == CM_PROB_DISABLED || config_flags & CONFIGFLAG_DISABLED != 0;
                node.parent_id = cm_parent(data.DevInst).and_then(cm_device_id);
                if !super::is_interface(&node) && !super::is_root_hub(&node) {
                    descendants(data.DevInst, 0, &mut node.descendants);
                }
                node.last_seen = property_filetime_ms(&set, &data, &DEVPKEY_Device_LastArrivalDate);
            } else {
                node.disabled = config_flags & CONFIGFLAG_DISABLED != 0;
                node.last_seen = property_filetime_ms(&set, &data, &DEVPKEY_Device_LastRemovalDate)
                    .or_else(|| property_filetime_ms(&set, &data, &DEVPKEY_Device_LastArrivalDate));
            }
            nodes.push(node);
        }
        Ok(assemble(nodes))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn node(id: &str, class: &str, desc: &str, present: bool) -> RawNode {
        RawNode {
            instance_id: id.into(),
            info: NodeInfo { class: class.into(), description: desc.into(), ..Default::default() },
            present,
            ..Default::default()
        }
    }

    #[test]
    fn groups_interfaces_and_hubs() {
        let root = node(r"USB\ROOT_HUB30\4&1", "USB", "USB Root Hub (USB 3.0)", true);
        let mut hub = node(r"USB\VID_05E3&PID_0610\5&2", "USB", "Generic USB Hub", true);
        hub.info.service = "USBHUB3".into();
        hub.parent_id = Some(root.instance_id.clone());

        let mut receiver = node(r"USB\VID_046D&PID_C52B\6&3&0&1", "USB", "USB Composite Device", true);
        receiver.info.bus_description = "USB Receiver".into();
        receiver.parent_id = Some(hub.instance_id.clone());
        receiver.location = "Port_#0001.Hub_#0004".into();
        receiver.descendants = vec![
            NodeInfo { class: "HIDClass".into(), description: "USB Input Device".into(), ..Default::default() },
            NodeInfo { class: "Keyboard".into(), description: "HID Keyboard Device".into(), ..Default::default() },
            NodeInfo { class: "Mouse".into(), description: "HID-compliant mouse".into(), ..Default::default() },
        ];
        let iface = node(r"USB\VID_046D&PID_C52B&MI_00\7&4", "HIDClass", "USB Input Device", true);

        let mut stick = node(r"USB\VID_0781&PID_5567\4C53", "USB", "USB Mass Storage Device", false);
        stick.info.compatible_ids = vec![r"USB\Class_08&SubClass_06&Prot_50".into()];
        stick.info.bus_description = "Cruzer Blade".into();
        stick.disabled = true;

        let out = assemble(vec![root, hub, receiver, iface, stick]);
        let ids: Vec<&str> = out.iter().map(|d| d.id.as_str()).collect();
        assert_eq!(ids, vec![r"USB\VID_05E3&PID_0610\5&2", r"USB\VID_046D&PID_C52B\6&3&0&1", r"USB\VID_0781&PID_5567\4C53"]);

        assert!(out[0].is_hub);
        assert!(out[0].hub_id.is_none(), "root hubs are not reported as a parent hub");

        let r = &out[1];
        assert_eq!(r.name, "Logitech USB Receiver");
        assert_eq!(r.category, Category::Input);
        assert_eq!(r.hub_id.as_deref(), Some(r"USB\VID_05E3&PID_0610\5&2"));
        assert_eq!(r.port, Some(1));
        assert!(r.enabled && r.present);
        assert_eq!(r.functions, vec!["HID Keyboard Device", "HID-compliant mouse"]);

        let s = &out[2];
        assert_eq!(s.category, Category::Storage);
        assert_eq!(s.name, "SanDisk Cruzer Blade");
        assert!(!s.enabled && !s.present);
    }
}
