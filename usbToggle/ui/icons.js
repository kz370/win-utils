// Line icons (24x24, stroke = currentColor).
(function () {
  const p = (d) => `<path d="${d}"/>`;
  const svg = (body) =>
    `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">${body}</svg>`;

  const icons = {
    usb: svg(
      '<circle cx="10" cy="7" r="1"/><circle cx="4" cy="20" r="1"/>' +
        p("M4.7 19.3 19 5") +
        p("m21 3-3 1 2 2Z") +
        p("M9.26 7.68 5 12l2 5") +
        p("m10 14 5 2 3.5-3.5") +
        p("m18 12 1-1 1 1-1 1Z")
    ),
    keyboard: svg(
      '<rect x="2" y="5" width="20" height="14" rx="2.5"/>' +
        p("M6 9h.01M10 9h.01M14 9h.01M18 9h.01M6 13h.01M10 13h.01M14 13h.01M18 13h.01M8 16h8")
    ),
    mouse: svg('<rect x="6" y="3" width="12" height="18" rx="6"/>' + p("M12 7v4")),
    input: svg(
      '<rect x="2" y="9" width="13" height="10" rx="2"/>' +
        p("M5 12.5h.01M8.5 12.5h.01M12 12.5h.01M6 15.5h6") +
        '<rect x="17" y="5" width="5" height="9" rx="2.5"/>' +
        p("M19.5 7.5v1.5")
    ),
    gamepad: svg(
      p("M6 11h4M8 9v4M15 12h.01M18 10h.01") +
        p(
          "M17.32 5H6.68a4 4 0 0 0-3.98 3.59C2.6 9.42 2 14.46 2 16a3 3 0 0 0 3 3c1 0 1.5-.5 2-1l1.41-1.41A2 2 0 0 1 9.83 16h4.34a2 2 0 0 1 1.41.59L17 18c.5.5 1 1 2 1a3 3 0 0 0 3-3c0-1.55-.6-6.58-.69-7.26A4 4 0 0 0 17.32 5z"
        )
    ),
    audio: svg(
      p("M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3")
    ),
    camera: svg('<circle cx="12" cy="10" r="7.5"/><circle cx="12" cy="10" r="3"/>' + p("M8 21.5h8M12 21.5v-4")),
    storage: svg(
      p("M22 12H2") +
        p("M5.45 5.11 2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z") +
        p("M6 16h.01M10 16h.01")
    ),
    phone: svg('<rect x="5" y="2" width="14" height="20" rx="2.5"/>' + p("M11 18h2")),
    bluetooth: svg(p("m7 7 10 10-5 5V2l5 5L7 17")),
    network: svg(
      '<rect x="16" y="16" width="6" height="6" rx="1"/><rect x="2" y="16" width="6" height="6" rx="1"/><rect x="9" y="2" width="6" height="6" rx="1"/>' +
        p("M5 16v-3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1v3M12 12V8")
    ),
    printer: svg(
      p("M6 9V3h12v6M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2") +
        '<rect x="6" y="14" width="12" height="7" rx="1"/>'
    ),
    serial: svg(p("M12 22v-5M9 8V2M15 8V2M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z")),
    security: svg(
      p("M2.59 17.41A2 2 0 0 0 2 18.83V21a1 1 0 0 0 1 1h3a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h1a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h.17a2 2 0 0 0 1.42-.59l.81-.81a6.5 6.5 0 1 0-4-4z") +
        '<circle cx="16.5" cy="7.5" r=".6" fill="currentColor"/>'
    ),
    hub: svg('<rect x="3" y="14" width="18" height="7" rx="2"/>' + p("M7 17.5h.01M11 17.5h.01M15 17.5h.01M12 14V3M9 6l3-3 3 3")),

    refresh: svg(p("M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8") + p("M21 3v5h-5")),
    settings: svg(
      p(
        "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"
      ) + '<circle cx="12" cy="12" r="3"/>'
    ),
    search: svg('<circle cx="11" cy="11" r="7"/>' + p("m20 20-3.5-3.5")),
    pin: svg(
      p("M12 17v5") +
        p("M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z")
    ),
    chevron: svg(p("m6 9 6 6 6-6")),
    lock: svg('<rect x="4" y="11" width="16" height="10" rx="2"/>' + p("M8 11V7a4 4 0 0 1 8 0v4")),
    unlock: svg('<rect x="4" y="11" width="16" height="10" rx="2"/>' + p("M8 11V7a4 4 0 0 1 7.75-1.4")),
    shield: svg(
      p("M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z") +
        p("M12 8v4M12 16h.01")
    ),
    copy: svg('<rect x="8" y="8" width="13" height="13" rx="2"/>' + p("M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2")),
    x: svg(p("M18 6 6 18M6 6l12 12")),
    alert: svg('<circle cx="12" cy="12" r="10"/>' + p("M12 8v4M12 16h.01")),
    check: svg(p("M20 6 9 17l-5-5")),
    info: svg('<circle cx="12" cy="12" r="10"/>' + p("M12 16v-4M12 8h.01")),
    unplugged: svg(p("m19 5 3-3M2 22l3-3M6.3 20.3a2.4 2.4 0 0 0 3.4 0L12 18l-6-6-2.3 2.3a2.4 2.4 0 0 0 0 3.4ZM7.5 13.5 10 11M10.5 16.5 13 14M12 6l6 6 2.3-2.3a2.4 2.4 0 0 0 0-3.4l-2.6-2.6a2.4 2.4 0 0 0-3.4 0Z")),
  };
  icons.other = icons.usb;

  window.ICONS = icons;
  window.icon = (name) => icons[name] || icons.usb;
})();
