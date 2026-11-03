use crate::pnp::DiscoveredDevice;
use crate::config::DeviceIdentity;

#[derive(Debug, Clone, PartialEq)]
pub enum MatchResultKind {
    None,
    Matched,
    Ambiguous,
}

pub struct DeviceMatch {
    pub kind: MatchResultKind,
    pub device: Option<DiscoveredDevice>,
    pub candidates: Vec<DiscoveredDevice>,
    pub reason: Option<String>,
}

pub struct DeviceMatcher;

impl DeviceMatcher {
    pub fn match_identity(identity: &DeviceIdentity, live: &[DiscoveredDevice]) -> DeviceMatch {
        let candidates = filter_by_identity(identity, live);

        if candidates.is_empty() {
            return DeviceMatch { kind: MatchResultKind::None, device: None, candidates, reason: Some("No live device matches the stored identity.".to_string()) };
        }

        if candidates.len() == 1 {
            return DeviceMatch { kind: MatchResultKind::Matched, device: Some(candidates[0].clone()), candidates, reason: None };
        }

        DeviceMatch {
            kind: MatchResultKind::Ambiguous,
            device: None,
            candidates,
            reason: Some(format!("Multiple live devices match identity {:?}:{:?}; refusing to pick arbitrarily.", identity.vid, identity.pid)),
        }
    }
}

fn filter_by_identity(identity: &DeviceIdentity, live: &[DiscoveredDevice]) -> Vec<DiscoveredDevice> {
    let has_serial = identity.serial.as_ref().map(|s| !s.is_empty()).unwrap_or(false);

    let exact: Vec<DiscoveredDevice> = live.iter().filter(|d| {
        let vid_pid = d.vid == identity.vid && d.pid == identity.pid;
        if !vid_pid { return false; }
        if has_serial {
            return d.serial.as_deref() == identity.serial.as_deref();
        }
        return d.location_paths.first().map(|l| l.eq_ignore_ascii_case(identity.location_path.as_deref().unwrap_or(""))).unwrap_or(false);
    }).cloned().collect();

    if !exact.is_empty() {
        return exact;
    }

    live.iter().filter(|d| {
        let vid_pid = d.vid == identity.vid && d.pid == identity.pid;
        if !vid_pid { return false; }
        if has_serial { return false; }
        d.location_paths.first().map(|l| l.eq_ignore_ascii_case(identity.location_path.as_deref().unwrap_or(""))).unwrap_or(false)
    }).cloned().collect()
}
