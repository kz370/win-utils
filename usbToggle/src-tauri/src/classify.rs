//! Pure helpers that turn raw PnP data into names and categories.
//! No Win32 calls here, so it is unit-testable on any platform.

use serde::Serialize;

#[derive(Debug, Clone, Default)]
pub struct NodeInfo {
    pub class: String,
    pub description: String,
    pub friendly_name: String,
    pub bus_description: String,
    pub service: String,
    pub hardware_ids: Vec<String>,
    pub compatible_ids: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Category {
    Keyboard,
    Mouse,
    Input,
    Gamepad,
    Audio,
    Camera,
    Storage,
    Phone,
    Bluetooth,
    Network,
    Printer,
    Serial,
    Security,
    Hub,
    Other,
}

impl Category {
    pub fn label(self) -> &'static str {
        match self {
            Category::Keyboard => "Keyboard",
            Category::Mouse => "Mouse",
            Category::Input => "Input device",
            Category::Gamepad => "Game controller",
            Category::Audio => "Audio device",
            Category::Camera => "Camera",
            Category::Storage => "Storage",
            Category::Phone => "Phone",
            Category::Bluetooth => "Bluetooth adapter",
            Category::Network => "Network adapter",
            Category::Printer => "Printer",
            Category::Serial => "Serial device",
            Category::Security => "Security key",
            Category::Hub => "USB hub",
            Category::Other => "USB device",
        }
    }
}

const GENERIC_NAMES: &[&str] = &[
    "usb composite device",
    "usb input device",
    "usb mass storage device",
    "usb attached scsi (uas) mass storage device",
    "generic usb hub",
    "usb hub",
    "usb root hub",
    "usb root hub (usb 3.0)",
    "generic superspeed usb hub",
    "usb device",
    "usb",
    "unknown device",
    "usb video device",
    "usb audio device",
    "usb printing support",
    "usb serial device",
    "hid-compliant device",
    "hid-compliant vendor-defined device",
    "hid-compliant consumer control device",
    "hid-compliant system controller",
    "hid keyboard device",
    "hid-compliant mouse",
    "winusb device",
    "mtp usb device",
    "disk drive",
];

const SKIP_FUNCTIONS: &[&str] = &[
    "usb composite device",
    "usb input device",
    "usb mass storage device",
    "hid-compliant vendor-defined device",
    "hid-compliant consumer control device",
    "hid-compliant system controller",
    "hid-compliant device",
    "generic volume",
    "volume",
    "winusb device",
];

fn is_generic(name: &str) -> bool {
    let n = name.trim().to_lowercase();
    n.is_empty()
        || n.len() < 2
        || GENERIC_NAMES.contains(&n.as_str())
        || n.starts_with("unknown usb device")
        || n.starts_with("usb\\")
}

fn ids_upper(info: &NodeInfo) -> impl Iterator<Item = String> + '_ {
    info.hardware_ids.iter().chain(info.compatible_ids.iter()).map(|s| s.to_ascii_uppercase())
}

/// True when any of the IDs advertises the given USB class code, e.g. "0E".
fn has_usb_class(ids: &[String], code: &str) -> bool {
    let needle = format!("CLASS_{code}");
    ids.iter().any(|id| {
        let mut rest = id.as_str();
        while let Some(pos) = rest.find(&needle) {
            let after = &rest[pos + needle.len()..];
            // Skip "SUBCLASS_xx"; only "CLASS_xx" / "DEVCLASS_xx" count.
            let is_subclass = rest[..pos].ends_with("SUB");
            if !is_subclass && (after.is_empty() || after.starts_with('&')) {
                return true;
            }
            rest = after;
        }
        false
    })
}

pub fn is_hub(info: &NodeInfo) -> bool {
    let svc = info.service.to_ascii_uppercase();
    if svc == "USBHUB" || svc == "USBHUB3" {
        return true;
    }
    let ids: Vec<String> = ids_upper(info).collect();
    has_usb_class(&ids, "09")
}

pub fn categorize(info: &NodeInfo, functions: &[NodeInfo]) -> Category {
    let classes: Vec<String> = std::iter::once(info)
        .chain(functions.iter())
        .map(|n| n.class.to_ascii_lowercase())
        .filter(|c| !c.is_empty())
        .collect();
    let ids: Vec<String> = std::iter::once(info).chain(functions.iter()).flat_map(ids_upper).collect();
    let has = |c: &str| classes.iter().any(|x| x == c);
    let id_has = |s: &str| ids.iter().any(|id| id.contains(s));

    let hint = format!(
        "{} {} {}",
        info.bus_description.to_lowercase(),
        info.friendly_name.to_lowercase(),
        info.description.to_lowercase()
    );
    let hint_has = |words: &[&str]| words.iter().any(|w| hint.contains(w));

    if has("camera") || has_usb_class(&ids, "0E") || (has("image") && hint_has(&["cam"])) {
        return Category::Camera;
    }
    if has("diskdrive") || has("cdrom") || has("scsiadapter") || has_usb_class(&ids, "08") {
        return Category::Storage;
    }
    if has("wpd")
        || has("androidusbdeviceclass")
        || hint_has(&["iphone", "ipad", "android", "phone", "galaxy", "pixel"])
        || has_usb_class(&ids, "06")
    {
        return Category::Phone;
    }
    if has("media") || has("audioendpoint") || has_usb_class(&ids, "01") {
        return Category::Audio;
    }
    if has("bluetooth") || id_has("CLASS_E0&SUBCLASS_01&PROT_01") {
        return Category::Bluetooth;
    }
    if has("net") || id_has("CLASS_E0&SUBCLASS_01&PROT_03") || id_has("CLASS_02&SUBCLASS_06") || id_has("CLASS_02&SUBCLASS_0D") {
        return Category::Network;
    }
    if has("printer") || has_usb_class(&ids, "07") {
        return Category::Printer;
    }
    if has("smartcardreader") || has("biometric") || has("securitydevices") || has_usb_class(&ids, "0B") {
        return Category::Security;
    }
    if has("xnacomposite")
        || has("xboxcomposite")
        || id_has("HID_DEVICE_UP:0001_U:0005")
        || id_has("HID_DEVICE_UP:0001_U:0004")
        || hint_has(&["controller", "gamepad", "joystick", "wheel"])
    {
        return Category::Gamepad;
    }

    let kb = has("keyboard") || id_has("CLASS_03&SUBCLASS_01&PROT_01");
    let ms = has("mouse") || id_has("CLASS_03&SUBCLASS_01&PROT_02");
    let says_mouse = hint_has(&["mouse", "trackball", "touchpad", "trackpad"]);
    let says_kb = hint_has(&["keyboard", "kbd", "keypad"]);
    match (kb, ms) {
        (true, true) if says_mouse && !says_kb => return Category::Mouse,
        (true, true) if says_kb && !says_mouse => return Category::Keyboard,
        (true, true) => return Category::Input,
        (true, false) if says_mouse => return Category::Mouse,
        (true, false) => return Category::Keyboard,
        (false, true) => return Category::Mouse,
        _ => {}
    }

    if has("ports") || has("modem") || has_usb_class(&ids, "02") {
        return Category::Serial;
    }
    if has("hidclass") || has_usb_class(&ids, "03") {
        if says_mouse {
            return Category::Mouse;
        }
        if says_kb {
            return Category::Keyboard;
        }
        return Category::Input;
    }
    Category::Other
}

fn clean(name: &str) -> String {
    let mut n = name.trim().to_string();
    for suffix in [" USB Device", " Usb Device", " USB device"] {
        if n.len() > suffix.len() + 2 && n.ends_with(suffix) {
            n.truncate(n.len() - suffix.len());
        }
    }
    n.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn best_of(n: &NodeInfo) -> Option<String> {
    [&n.friendly_name, &n.description]
        .into_iter()
        .find(|s| !is_generic(s))
        .map(|s| clean(s))
}

pub fn display_name(info: &NodeInfo, functions: &[NodeInfo], category: Category, vendor: Option<&str>) -> String {
    let wanted_class: &[&str] = match category {
        Category::Storage => &["diskdrive", "cdrom"],
        Category::Camera => &["camera", "image"],
        Category::Phone => &["wpd"],
        _ => &[],
    };
    let from_function = || {
        functions
            .iter()
            .filter(|f| wanted_class.contains(&f.class.to_ascii_lowercase().as_str()))
            .find_map(best_of)
    };

    let mut name = from_function()
        .or_else(|| (!is_generic(&info.bus_description)).then(|| clean(&info.bus_description)))
        .or_else(|| best_of(info))
        .or_else(|| {
            functions
                .iter()
                .filter(|f| !matches!(f.class.to_ascii_lowercase().as_str(), "hidclass" | "volume" | "softwaredevice"))
                .find_map(best_of)
        })
        .unwrap_or_else(|| match vendor {
            Some(v) => format!("{v} {}", category.label()),
            None => category.label().to_string(),
        });

    if let Some(v) = vendor {
        let lower = name.to_lowercase();
        let first = v.split_whitespace().next().unwrap_or(v).to_lowercase();
        let generic_start = ["usb", "wireless", "gaming", "optical", "generic"].iter().any(|w| lower.starts_with(w));
        // Short product strings ("Cruzer Blade") read better with the brand.
        let short = name.split_whitespace().count() <= 2;
        if (generic_start || short) && !lower.contains(&first) && !lower.starts_with(&v.to_lowercase())
        {
            name = format!("{v} {name}");
        }
    }
    name
}

/// Human name of a function below a device, for the details panel.
pub fn function_label(f: &NodeInfo) -> Option<String> {
    let class = f.class.to_ascii_lowercase();
    if matches!(class.as_str(), "volume" | "volumesnapshot" | "softwarecomponent") {
        return None;
    }
    let name = if !f.friendly_name.trim().is_empty() { &f.friendly_name } else { &f.description };
    let n = name.trim();
    if n.is_empty() || SKIP_FUNCTIONS.contains(&n.to_lowercase().as_str()) {
        return None;
    }
    Some(clean(n))
}

pub fn parse_vid_pid(instance_id: &str) -> (Option<String>, Option<String>) {
    let upper = instance_id.to_ascii_uppercase();
    let part = upper.split('\\').nth(1).unwrap_or("");
    let grab = |key: &str| -> Option<String> {
        let start = part.find(key)? + key.len();
        let v: String = part[start..].chars().take_while(|c| c.is_ascii_hexdigit()).collect();
        (v.len() == 4).then_some(v)
    };
    (grab("VID_"), grab("PID_"))
}

/// Which side of the case a port is on, from the ACPI _PLD buffer that
/// Windows exposes as DEVPKEY_Device_PhysicalDeviceLocation. Only as good as
/// the motherboard firmware: some boards report nothing or "back" for all.
pub fn parse_pld_panel(pld: &[u8]) -> Option<&'static str> {
    // Bits 64..95: UserVisible(64) Dock(65) Lid(66) Panel(67-69) ...
    let d2 = u32::from_le_bytes(pld.get(8..12)?.try_into().ok()?);
    let revision = pld.first()? & 0x7F;
    if revision == 0 {
        return None;
    }
    Some(match (d2 >> 3) & 0x7 {
        0 => "top",
        1 => "bottom",
        2 => "left",
        3 => "right",
        4 => "front",
        5 => "back",
        _ => return None,
    })
}

/// "Port_#0003.Hub_#0001" -> Some(3)
pub fn parse_port(location: &str) -> Option<u32> {
    let start = location.find("Port_#")? + "Port_#".len();
    let digits: String = location[start..].chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().ok().filter(|&p| p > 0)
}

pub fn vendor_name(vid: &str) -> Option<&'static str> {
    Some(match vid.to_ascii_uppercase().as_str() {
        "03F0" => "HP",
        "0403" => "FTDI",
        "040B" => "Weltrend",
        "041E" => "Creative",
        "0424" => "Microchip",
        "044F" => "Thrustmaster",
        "045E" => "Microsoft",
        "046A" => "Cherry",
        "046D" => "Logitech",
        "047F" => "Poly",
        "0480" => "Toshiba",
        "0489" => "Foxconn",
        "04A9" => "Canon",
        "04B8" => "Epson",
        "04CA" => "Lite-On",
        "04D9" => "Holtek",
        "04E8" => "Samsung",
        "04F2" => "Chicony",
        "04F3" => "Elan",
        "0502" => "Acer",
        "054C" => "Sony",
        "056A" => "Wacom",
        "057E" => "Nintendo",
        "058F" => "Alcor Micro",
        "05AC" => "Apple",
        "05C6" => "Qualcomm",
        "05E3" => "Genesys Logic",
        "067B" => "Prolific",
        "06CB" => "Synaptics",
        "0781" => "SanDisk",
        "07CA" => "AVerMedia",
        "0930" => "Toshiba",
        "093A" => "PixArt",
        "0951" => "Kingston",
        "09DA" => "A4Tech",
        "0A12" => "Cambridge Silicon Radio",
        "0A5C" => "Broadcom",
        "0B05" => "ASUS",
        "0B0E" => "Jabra",
        "0B95" => "ASIX",
        "0BC2" => "Seagate",
        "0BDA" => "Realtek",
        "0C45" => "Microdia",
        "0CF3" => "Qualcomm Atheros",
        "0D8C" => "C-Media",
        "0E8D" => "MediaTek",
        "0FD9" => "Elgato",
        "1004" => "LG",
        "1038" => "SteelSeries",
        "1050" => "Yubico",
        "1058" => "Western Digital",
        "10C4" => "Silicon Labs",
        "12D1" => "Huawei",
        "138A" => "Validity Sensors",
        "13D3" => "IMC Networks",
        "13FE" => "Phison",
        "1532" => "Razer",
        "152D" => "JMicron",
        "17EF" => "Lenovo",
        "18D1" => "Google",
        "1A40" => "Terminus Technology",
        "1A86" => "WCH",
        "1B1C" => "Corsair",
        "1BCF" => "Sunplus",
        "1D6B" => "Linux Foundation",
        "1E7D" => "ROCCAT",
        "2109" => "VIA Labs",
        "2341" => "Arduino",
        "2357" => "TP-Link",
        "2717" => "Xiaomi",
        "27C6" => "Goodix",
        "28DE" => "Valve",
        "2A70" => "OnePlus",
        "2C7C" => "Quectel",
        "2DC8" => "8BitDo",
        "2E8A" => "Raspberry Pi",
        "3434" => "Keychron",
        "413C" => "Dell",
        "8087" => "Intel",
        "8086" => "Intel",
        "8564" => "Transcend",
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn node(class: &str, desc: &str, ids: &[&str]) -> NodeInfo {
        NodeInfo {
            class: class.into(),
            description: desc.into(),
            compatible_ids: ids.iter().map(|s| s.to_string()).collect(),
            ..Default::default()
        }
    }

    #[test]
    fn vid_pid() {
        assert_eq!(
            parse_vid_pid(r"USB\VID_046D&PID_C52B\5&1A2B&0&3"),
            (Some("046D".into()), Some("C52B".into()))
        );
        assert_eq!(parse_vid_pid(r"USB\ROOT_HUB30\4&1"), (None, None));
    }

    fn pld(panel: u32) -> Vec<u8> {
        let mut b = vec![0u8; 16];
        b[0] = 2; // revision
        b[8..12].copy_from_slice(&((1 | (panel << 3)) as u32).to_le_bytes());
        b
    }

    #[test]
    fn panel() {
        assert_eq!(parse_pld_panel(&pld(4)), Some("front"));
        assert_eq!(parse_pld_panel(&pld(5)), Some("back"));
        assert_eq!(parse_pld_panel(&pld(2)), Some("left"));
        assert_eq!(parse_pld_panel(&pld(6)), None, "unknown");
        assert_eq!(parse_pld_panel(&[2, 0, 0]), None, "too short");
    }

    #[test]
    fn port() {
        assert_eq!(parse_port("Port_#0003.Hub_#0001"), Some(3));
        assert_eq!(parse_port("0000.0014.0000.003.000"), None);
    }

    #[test]
    fn categories() {
        let composite = node("USB", "USB Composite Device", &[r"USB\COMPOSITE"]);
        let cam = node("Camera", "USB Video Device", &[r"USB\Class_0E&SubClass_03&Prot_00"]);
        assert_eq!(categorize(&composite, &[cam]), Category::Camera);

        let kb = node("Keyboard", "HID Keyboard Device", &[]);
        let ms = node("Mouse", "HID-compliant mouse", &[]);
        let mut receiver = composite.clone();
        receiver.bus_description = "USB Receiver".into();
        assert_eq!(categorize(&receiver, &[kb.clone(), ms.clone()]), Category::Input);
        let mut mouse = composite.clone();
        mouse.bus_description = "G502 HERO Gaming Mouse".into();
        assert_eq!(categorize(&mouse, &[kb, ms]), Category::Mouse);

        let disk = node("USB", "USB Mass Storage Device", &[r"USB\Class_08&SubClass_06&Prot_50"]);
        assert_eq!(categorize(&disk, &[]), Category::Storage);

        let bt = node("Bluetooth", "Intel(R) Wireless Bluetooth(R)", &[r"USB\Class_E0&SubClass_01&Prot_01"]);
        assert_eq!(categorize(&bt, &[]), Category::Bluetooth);

        // Class 01 must not match class 010 or similar.
        assert!(!has_usb_class(&[r"USB\CLASS_010".into()], "01"));
        assert!(has_usb_class(&[r"USB\DEVCLASS_09&SUBCLASS_00".into()], "09"));
        assert!(!has_usb_class(&[r"USB\CLASS_E0&SUBCLASS_01&PROT_01".into()], "01"));
    }

    #[test]
    fn names() {
        let mut storage = node("USB", "USB Mass Storage Device", &[r"USB\Class_08"]);
        storage.bus_description = "Cruzer Blade".into();
        let disk = NodeInfo { class: "DiskDrive".into(), friendly_name: "SanDisk Cruzer Blade USB Device".into(), ..Default::default() };
        assert_eq!(display_name(&storage, &[disk], Category::Storage, Some("SanDisk")), "SanDisk Cruzer Blade");

        let mut receiver = node("USB", "USB Composite Device", &[]);
        receiver.bus_description = "USB Receiver".into();
        assert_eq!(display_name(&receiver, &[], Category::Input, Some("Logitech")), "Logitech USB Receiver");

        let unknown = node("USB", "USB Composite Device", &[]);
        assert_eq!(display_name(&unknown, &[], Category::Other, Some("Realtek")), "Realtek USB device");
        assert_eq!(display_name(&unknown, &[], Category::Other, None), "USB device");
    }
}
