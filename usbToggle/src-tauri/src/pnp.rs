use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::ptr;

const DIGCF_DEFAULT: u32 = 0x00000001;
const DIGCF_PRESENT: u32 = 0x00000002;
const DIGCF_ALLCLASSES: u32 = 0x00000004;

const SPDRP_DEVICEDESC: u32 = 0x00000000;
const SPDRP_HARDWAREID: u32 = 0x00000001;
const SPDRP_SERVICE: u32 = 0x00000009;
const SPDRP_CONTAINERID: u32 = 0x0000001F;
const SPDRP_LOCATION_PATHS: u32 = 0x0000003E;

const CR_SUCCESS: u32 = 0;
const DN_IS_PRESENT: u32 = 0x00000001;

const INVALID_HANDLE_VALUE: isize = -1;

type Guid = [u8; 16];

#[repr(C)]
struct SpDevInfoData {
    cb_size: u32,
    class_guid: Guid,
    dev_inst: u32,
    reserved: usize,
}

#[link(name = "setupapi")]
extern "system" {
    fn SetupDiGetClassDevsW(class_guid: *const Guid, enumerator: *const u16, hwnd_parent: isize, flags: u32) -> isize;
    fn SetupDiEnumDeviceInfo(device_info_set: isize, member_index: u32, device_info_data: *mut SpDevInfoData) -> i32;
    fn SetupDiGetDeviceInstanceIdW(device_info_set: isize, device_info_data: *mut SpDevInfoData, instance_id: *mut u16, size: u32, required_size: *mut u32) -> i32;
    fn SetupDiGetDeviceRegistryPropertyW(device_info_set: isize, device_info_data: *const SpDevInfoData, property: u32, property_type: *mut u32, property_buffer: *mut u8, buffer_size: u32, required_size: *mut u32) -> i32;
    fn SetupDiDestroyDeviceInfoList(device_info_set: isize) -> i32;
}

#[link(name = "cfgmgr32")]
extern "system" {
    fn CM_Get_DevNode_Status(status: *mut u32, problem: *mut u32, dev_inst: u32, flags: u32) -> u32;
    fn CM_Get_Parent(parent_dev_inst: *mut u32, child_dev_inst: u32, flags: u32) -> u32;
    fn CM_Get_Device_IDW(dev_inst: u32, buffer: *mut u16, buffer_size: i32, flags: u32) -> u32;
}

fn to_wide(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(Some(0)).collect()
}

fn read_wide_ptr(buf: *const u16) -> String {
    if buf.is_null() {
        return String::new();
    }
    let mut len = 0;
    unsafe {
        while *buf.add(len) != 0 {
            len += 1;
        }
    }
    String::from_utf16_lossy(unsafe { std::slice::from_raw_parts(buf, len) })
}

fn read_instance_id(device_info_set: isize, data: &mut SpDevInfoData) -> String {
    let mut required = 0u32;
    unsafe {
        if SetupDiGetDeviceInstanceIdW(device_info_set, data, ptr::null_mut(), 0, &mut required) == 0 && required == 0 {
            return String::new();
        }
    }
    let mut buf: Vec<u16> = vec![0; required as usize];
    unsafe {
        if SetupDiGetDeviceInstanceIdW(device_info_set, data, buf.as_mut_ptr(), buf.len() as u32, &mut required) == 0 {
            return String::new();
        }
    }
    read_wide_ptr(buf.as_ptr())
}

fn read_registry_string(device_info_set: isize, data: &SpDevInfoData, property: u32) -> Option<String> {
    let mut required = 0u32;
    let mut _typ = 0u32;
    unsafe {
        if SetupDiGetDeviceRegistryPropertyW(device_info_set, data, property, &mut _typ as *mut u32, ptr::null_mut() as *mut u8, 0, &mut required as *mut u32) == 0 && required == 0 {
            return None;
        }
    }
    let mut buf: Vec<u8> = vec![0; required as usize];
    unsafe {
        if SetupDiGetDeviceRegistryPropertyW(device_info_set, data, property, &mut _typ as *mut u32, buf.as_mut_ptr() as *mut u8, buf.len() as u32, &mut required as *mut u32) == 0 {
            return None;
        }
    }
    Some(read_wide_ptr(buf.as_ptr() as *const u16))
}

fn read_registry_multi_string(device_info_set: isize, data: &SpDevInfoData, property: u32) -> Vec<String> {
    let mut required = 0u32;
    let mut _typ = 0u32;
    unsafe {
        if SetupDiGetDeviceRegistryPropertyW(device_info_set, data, property, &mut _typ as *mut u32, ptr::null_mut() as *mut u8, 0, &mut required as *mut u32) == 0 {
            return Vec::new();
        }
    }
    let mut buf: Vec<u16> = vec![0; required as usize / 2];
    unsafe {
        if SetupDiGetDeviceRegistryPropertyW(device_info_set, data, property, &mut _typ as *mut u32, buf.as_mut_ptr() as *mut u8, buf.len() as u32 * 2, &mut required as *mut u32) == 0 {
            return Vec::new();
        }
    }
    let text = read_wide_ptr(buf.as_ptr());
    text.split('\0').filter(|s| !s.is_empty()).map(|s| s.to_string()).collect()
}

fn cm_device_id(dev_inst: u32) -> Option<String> {
    let mut buf: Vec<u16> = vec![0; 512];
    unsafe {
        if CM_Get_Device_IDW(dev_inst, buf.as_mut_ptr(), buf.len() as i32, 0) != CR_SUCCESS {
            return None;
        }
    }
    Some(read_wide_ptr(buf.as_ptr()))
}

fn cm_parent(dev_inst: u32) -> Option<u32> {
    let mut parent = 0u32;
    unsafe {
        if CM_Get_Parent(&mut parent, dev_inst, 0) != CR_SUCCESS {
            return None;
        }
    }
    if parent == 0 { None } else { Some(parent) }
}

fn cm_status(dev_inst: u32) -> (u32, u32) {
    let mut status = 0u32;
    let mut problem = 0u32;
    unsafe {
        if CM_Get_DevNode_Status(&mut status, &mut problem, dev_inst, 0) != CR_SUCCESS {
            return (0, 0);
        }
    }
    (status, problem)
}

#[derive(Clone)]
pub struct DiscoveredDevice {
    pub instance_id: String,
    pub device_desc: String,
    pub hardware_ids: Vec<String>,
    pub service: String,
    pub location_paths: Vec<String>,
    pub container_id: String,
    pub dev_node_status: u32,
    pub problem: u32,
    pub vid: Option<String>,
    pub pid: Option<String>,
    pub serial: Option<String>,
    pub is_hub: bool,
    pub parent_dev_inst: u32,
    pub dev_inst: u32,
}

impl DiscoveredDevice {
    pub fn is_present(&self) -> bool {
        (self.dev_node_status & DN_IS_PRESENT) != 0
    }
}

pub struct PnpCatalog;

impl PnpCatalog {
    pub fn scan(flags: u32) -> Vec<DiscoveredDevice> {
        let mut devices: Vec<DiscoveredDevice> = Vec::new();
        let all_classes: Guid = [0; 16];
        unsafe {
            let hdev_info = SetupDiGetClassDevsW(&all_classes, ptr::null(), 0, flags);
            if hdev_info == INVALID_HANDLE_VALUE {
                return devices;
            }

            let mut index: u32 = 0;
            loop {
                let mut data = SpDevInfoData {
                    cb_size: std::mem::size_of::<SpDevInfoData>() as u32,
                    class_guid: [0; 16],
                    dev_inst: 0,
                    reserved: 0,
                };
                if SetupDiEnumDeviceInfo(hdev_info, index, &mut data) == 0 {
                    break;
                }
                index += 1;

                let dev = match read_device(hdev_info, &mut data) {
                    Some(d) => d,
                    None => continue,
                };
                if !is_usb_related(&dev) {
                    continue;
                }
                devices.push(dev);
            }

            SetupDiDestroyDeviceInfoList(hdev_info);
        }
        devices
    }

    pub fn scan_present() -> Vec<DiscoveredDevice> {
        Self::scan(DIGCF_ALLCLASSES | DIGCF_PRESENT)
    }

    pub fn scan_retained() -> Vec<DiscoveredDevice> {
        Self::scan(DIGCF_ALLCLASSES)
    }
}

fn read_device(device_info_set: isize, data: &mut SpDevInfoData) -> Option<DiscoveredDevice> {
    let instance_id = read_instance_id(device_info_set, data);
    if instance_id.is_empty() {
        return None;
    }

    let device_desc = read_registry_string(device_info_set, data, SPDRP_DEVICEDESC).unwrap_or_default();
    let hardware_ids = read_registry_multi_string(device_info_set, data, SPDRP_HARDWAREID);
    let service = read_registry_string(device_info_set, data, SPDRP_SERVICE).unwrap_or_default();
    let location_paths = read_registry_multi_string(device_info_set, data, SPDRP_LOCATION_PATHS);
    let container_id = read_registry_string(device_info_set, data, SPDRP_CONTAINERID).unwrap_or_default();

    let (status, problem) = cm_status(data.dev_inst);

    let mut dev = DiscoveredDevice {
        instance_id,
        device_desc,
        hardware_ids,
        service,
        location_paths,
        container_id,
        dev_node_status: status,
        problem,
        vid: None,
        pid: None,
        serial: None,
        is_hub: false,
        parent_dev_inst: 0,
        dev_inst: data.dev_inst,
    };
    parse_identity(&mut dev);
    Some(dev)
}

fn parse_identity(dev: &mut DiscoveredDevice) {
    let mut vid: Option<String> = None;
    let mut pid: Option<String> = None;
    for hw in &dev.hardware_ids {
        let h = hw.trim().to_uppercase();
        let vid_idx = h.find("VID_");
        let pid_idx = h.find("PID_");
        if let (Some(v), Some(p)) = (vid_idx, pid_idx) {
            if p > v {
                vid = Some(h[v + 4..v + 8].to_string());
                let end = h[p..].find('&').map(|e| p + e).unwrap_or(h.len());
                pid = Some(h[p + 4..end].to_string());
                break;
            }
        }
    }
    dev.vid = vid;
    dev.pid = pid;

    let mut serial: Option<String> = None;
    if dev.instance_id.to_uppercase().starts_with("USB\\") {
        if let Some(last) = dev.instance_id.rfind('\\') {
            let candidate = &dev.instance_id[last + 1..];
            if candidate.len() >= 3 {
                serial = Some(candidate.to_string());
            }
        }
    }
    dev.serial = serial;
    dev.is_hub = dev.service.eq_ignore_ascii_case("USBHUB3") || dev.service.eq_ignore_ascii_case("USBHUB");
}

fn is_usb_related(dev: &DiscoveredDevice) -> bool {
    if dev.is_hub {
        return true;
    }
    if dev.vid.is_some() || dev.pid.is_some() {
        return true;
    }
    dev.hardware_ids.iter().any(|h| h.trim().to_uppercase().starts_with("USB\\"))
}

pub fn build_hub_tree(devices: &[DiscoveredDevice]) -> Vec<(DiscoveredDevice, Vec<DiscoveredDevice>)> {
    let hubs: Vec<DiscoveredDevice> = devices.iter().filter(|d| d.is_hub).cloned().collect();
    let hub_by_inst: std::collections::HashMap<String, DiscoveredDevice> = hubs.iter().map(|h| (h.instance_id.clone(), h.clone())).collect();

    let mut result: Vec<(DiscoveredDevice, Vec<DiscoveredDevice>)> = Vec::new();
    for hub in &hubs {
        let mut children: Vec<DiscoveredDevice> = Vec::new();
        for dev in devices.iter().filter(|d| !d.is_hub) {
            if let Some(parent_hub_id) = find_ancestor_hub_id(dev, &hub_by_inst) {
                if parent_hub_id == hub.instance_id {
                    children.push(dev.clone());
                }
            }
        }
        result.push((hub.clone(), children));
    }

    let assigned: std::collections::HashSet<String> = result.iter().flat_map(|(_, c)| c.iter().map(|d| d.instance_id.clone())).collect();
    let other: Vec<DiscoveredDevice> = devices.iter().filter(|d| !d.is_hub && !assigned.contains(&d.instance_id)).cloned().collect();
    if !other.is_empty() {
        result.push((
            DiscoveredDevice {
                instance_id: "other".to_string(),
                device_desc: "Other USB devices".to_string(),
                hardware_ids: Vec::new(),
                service: "OTHER".to_string(),
                location_paths: Vec::new(),
                container_id: String::new(),
                dev_node_status: 0,
                problem: 0,
                vid: None,
                pid: None,
                serial: None,
                is_hub: false,
                parent_dev_inst: 0,
                dev_inst: 0,
            },
            other,
        ));
    }

    result
}

fn find_ancestor_hub_id(dev: &DiscoveredDevice, hub_by_inst: &std::collections::HashMap<String, DiscoveredDevice>) -> Option<String> {
    let mut parent = dev.dev_inst;
    let mut hops = 0;
    while parent != 0 && hops < 32 {
        if let Some(parent_id) = cm_device_id(parent) {
            if hub_by_inst.contains_key(&parent_id) {
                return Some(parent_id);
            }
        }
        match cm_parent(parent) {
            Some(p) => parent = p,
            None => break,
        }
        hops += 1;
    }
    None
}
