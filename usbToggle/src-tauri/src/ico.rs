//! Picks one frame out of a multi-size .ico file.
//!
//! The tray icon is `assets/tray_icon.ico`. Windows wants the frame that
//! matches the screen scale (16 px at 100%, 24 px at 150%, ...), so the
//! sharpest frame is selected here and handed to Tauri as a one-frame icon.

const HEADER: usize = 6;
const ENTRY: usize = 16;

/// Returns a one-frame .ico containing the smallest frame that is at least
/// `want` pixels wide, or the largest frame when none is big enough.
/// `None` when the data is not a valid icon file.
pub fn pick_frame(ico: &[u8], want: u32) -> Option<Vec<u8>> {
    let u16_at = |o: usize| ico.get(o..o + 2).map(|b| u16::from_le_bytes([b[0], b[1]]));
    let u32_at = |o: usize| ico.get(o..o + 4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]));

    if u16_at(0)? != 0 || u16_at(2)? != 1 {
        return None; // not an icon (type 1)
    }
    let count = u16_at(4)? as usize;
    if count == 0 || count > 256 {
        return None;
    }

    // (width in pixels, index of the entry)
    let mut best: Option<(u32, usize)> = None;
    for i in 0..count {
        let at = HEADER + i * ENTRY;
        let width = match *ico.get(at)? {
            0 => 256,
            w => w as u32,
        };
        let size = u32_at(at + 8)? as usize;
        let offset = u32_at(at + 12)? as usize;
        if size == 0 || offset.checked_add(size)? > ico.len() {
            continue; // damaged entry: ignore it
        }
        best = match best {
            None => Some((width, i)),
            Some((bw, _)) if bw < want && width > bw => Some((width, i)), // still too small: bigger is better
            Some((bw, _)) if width >= want && (bw < want || width < bw) => Some((width, i)), // closest above `want`
            keep => keep,
        };
    }

    let (_, index) = best?;
    let at = HEADER + index * ENTRY;
    let size = u32_at(at + 8)? as usize;
    let offset = u32_at(at + 12)? as usize;

    let data_start = HEADER + ENTRY;
    let mut out = Vec::with_capacity(data_start + size);
    out.extend_from_slice(&[0, 0, 1, 0, 1, 0]); // header: icon, one image
    out.extend_from_slice(&ico[at..at + 12]); // entry up to the offset field
    out.extend_from_slice(&(data_start as u32).to_le_bytes());
    out.extend_from_slice(&ico[offset..offset + size]);
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Builds an .ico whose frames are the given (width, payload) pairs.
    fn make_ico(frames: &[(u32, &[u8])]) -> Vec<u8> {
        let mut out = vec![0, 0, 1, 0];
        out.extend_from_slice(&(frames.len() as u16).to_le_bytes());
        let mut offset = HEADER + ENTRY * frames.len();
        for (w, data) in frames {
            let d = if *w >= 256 { 0 } else { *w as u8 };
            out.extend_from_slice(&[d, d, 0, 0, 1, 0, 32, 0]);
            out.extend_from_slice(&(data.len() as u32).to_le_bytes());
            out.extend_from_slice(&(offset as u32).to_le_bytes());
            offset += data.len();
        }
        for (_, data) in frames {
            out.extend_from_slice(data);
        }
        out
    }

    fn payload(frame: &[u8]) -> &[u8] {
        // one-frame result: header (6) + entry (16) + data
        &frame[HEADER + ENTRY..]
    }

    #[test]
    fn picks_the_smallest_frame_that_is_big_enough() {
        let ico = make_ico(&[(16, b"sixteen"), (24, b"twentyfour"), (32, b"thirtytwo"), (64, b"sixtyfour")]);
        assert_eq!(payload(&pick_frame(&ico, 16).unwrap()), b"sixteen");
        assert_eq!(payload(&pick_frame(&ico, 24).unwrap()), b"twentyfour");
        assert_eq!(payload(&pick_frame(&ico, 25).unwrap()), b"thirtytwo");
        assert_eq!(payload(&pick_frame(&ico, 48).unwrap()), b"sixtyfour");
    }

    #[test]
    fn falls_back_to_the_largest_frame() {
        let ico = make_ico(&[(16, b"a"), (32, b"bb")]);
        assert_eq!(payload(&pick_frame(&ico, 128).unwrap()), b"bb");
    }

    #[test]
    fn frame_order_does_not_matter_and_256_is_stored_as_zero() {
        let ico = make_ico(&[(256, b"big"), (16, b"small"), (48, b"mid")]);
        assert_eq!(payload(&pick_frame(&ico, 20).unwrap()), b"mid");
        assert_eq!(payload(&pick_frame(&ico, 200).unwrap()), b"big");
    }

    #[test]
    fn result_is_a_valid_one_frame_icon() {
        let ico = make_ico(&[(16, b"one"), (32, b"two")]);
        let out = pick_frame(&ico, 32).unwrap();
        assert_eq!(&out[..6], &[0, 0, 1, 0, 1, 0]);
        assert_eq!(out[6], 32); // width of the kept frame
        assert_eq!(u32::from_le_bytes([out[18], out[19], out[20], out[21]]), 22); // offset of its data
        assert_eq!(payload(&out), b"two");
        // the result can itself be read again
        assert_eq!(payload(&pick_frame(&out, 16).unwrap()), b"two");
    }

    #[test]
    fn rejects_files_that_are_not_icons() {
        assert!(pick_frame(&[], 16).is_none());
        assert!(pick_frame(b"\x89PNG\r\n\x1a\n", 16).is_none());
        assert!(pick_frame(&[0, 0, 2, 0, 1, 0], 16).is_none(), "cursor files are not icons");
        assert!(pick_frame(&[0, 0, 1, 0, 0, 0], 16).is_none(), "no frames");
    }

    #[test]
    fn skips_damaged_entries() {
        let mut ico = make_ico(&[(16, b"good"), (32, b"bad")]);
        // Point the second frame's data past the end of the file.
        let at = HEADER + ENTRY + 12;
        ico[at..at + 4].copy_from_slice(&9999u32.to_le_bytes());
        assert_eq!(payload(&pick_frame(&ico, 32).unwrap()), b"good");
    }
}
