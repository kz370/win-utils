(() => {
  "use strict";

  // ------------------------------------------------------------ bridge --

  const tauri = window.__TAURI__;
  const invoke = (cmd, args) => tauri.core.invoke(cmd, args);
  const listen = (name, cb) => tauri.event.listen(name, (e) => cb(e.payload));

  // ------------------------------------------------------------ consts --

  const CATEGORY = {
    keyboard: "Keyboard",
    mouse: "Mouse",
    input: "Keyboard & mouse",
    gamepad: "Game controller",
    audio: "Audio",
    camera: "Camera",
    storage: "Storage",
    phone: "Phone",
    bluetooth: "Bluetooth",
    network: "Network",
    printer: "Printer",
    serial: "Serial / COM",
    security: "Security key",
    hub: "USB hub",
    other: "USB device",
  };

  // Where the port is on the case (from the motherboard firmware).
  const PANEL = {
    front: "Front panel",
    back: "Back panel",
    left: "Left side",
    right: "Right side",
    top: "Top",
    bottom: "Bottom",
  };
  const PANEL_ORDER = ["front", "back", "left", "right", "top", "bottom"];
  const panelLabel = (d) => (d.panel && PANEL[d.panel]) || null;

  // Icons a user can pick for a device. Extra icons borrow a category's color.
  const ICON_CHOICES = [
    ["other", "USB"],
    ["keyboard", "Keyboard"],
    ["mouse", "Mouse"],
    ["input", "Keyboard & mouse"],
    ["gamepad", "Game controller"],
    ["pen", "Pen tablet"],
    ["audio", "Headphones"],
    ["mic", "Microphone"],
    ["speaker", "Speaker"],
    ["camera", "Camera"],
    ["monitor", "Monitor"],
    ["storage", "Drive"],
    ["card", "Card reader"],
    ["phone", "Phone"],
    ["bluetooth", "Bluetooth"],
    ["network", "Network"],
    ["printer", "Printer"],
    ["serial", "Cable"],
    ["security", "Security key"],
    ["light", "Lights"],
    ["fan", "Fan / cooler"],
    ["hub", "Hub"],
  ];
  const ICON_COLOR = { mic: "audio", speaker: "audio", monitor: "input", pen: "input", card: "storage", light: "gamepad", fan: "network" };
  const autoIconKey = (d) => (d.category in CATEGORY ? d.category : "other");
  const knownIcon = (key) => !!key && (!!window.ICONS[key] || !!(window.ICON_LIBRARY && window.ICON_LIBRARY.has(key)));
  const iconKey = (d) => (knownIcon(d.customIcon) ? d.customIcon : autoIconKey(d));
  const colorOf = (key) =>
    ICON_COLOR[key] || (window.ICON_LIBRARY && window.ICON_LIBRARY.has(key) ? window.ICON_LIBRARY.color(key) : key);
  const iconLabel = (key) => {
    const builtIn = ICON_CHOICES.find(([k]) => k === key);
    if (builtIn) return builtIn[1];
    const name = key.startsWith("l-") ? key.slice(2) : key;
    return name.charAt(0).toUpperCase() + name.slice(1).replace(/-/g, " ");
  };

  const RISKY = new Set(["keyboard", "mouse", "input", "hub", "bluetooth"]);

  // Device Manager problem codes, phrased for people.
  const PROBLEMS = {
    1: "Windows couldn't configure this device.",
    3: "The driver may be damaged, or Windows is low on memory.",
    10: "The device couldn't start.",
    12: "Not enough free resources for this device.",
    14: "Restart Windows to finish setting up this device.",
    18: "The drivers for this device need to be reinstalled.",
    19: "Windows can't start this device: its configuration is incomplete or damaged.",
    21: "Windows is removing this device.",
    24: "The device isn't present or isn't working properly.",
    28: "No driver is installed for this device.",
    29: "The device is turned off in the firmware.",
    31: "The device isn't working properly. Windows couldn't load its drivers.",
    32: "The driver service for this device is disabled.",
    37: "Windows couldn't initialise the driver.",
    39: "Windows couldn't load the driver. It may be missing or damaged.",
    41: "The driver loaded but Windows can't find the hardware.",
    43: "Windows stopped this device because it reported a problem.",
    45: "The device isn't connected.",
    47: "The device was prepared for safe removal. Unplug it and plug it back in.",
    48: "The driver is blocked because it has known issues.",
    52: "Windows can't verify the driver's digital signature.",
  };

  const DEFAULT_SETTINGS = {
    theme: "system",
    closeToTray: true,
    startMinimized: true,
    showDisconnected: false,
    showHubs: false,
    portsOnly: true,
    confirmRisky: true,
    hideProblems: true,
    protectInput: true,
  };

  // ------------------------------------------------------------- state --

  const state = {
    loaded: false,
    devices: [],
    byId: new Map(),
    busy: new Set(),
    pending: new Map(), // id -> target enabled
    scannedAt: 0,
    scanError: null,
    info: null,
    settings: { ...DEFAULT_SETTINGS },
    filter: "all",
    query: "",
    expanded: new Set(),
    collapsed: { disconnected: true, hubs: false },
    refreshing: false,
  };

  const store = {
    get(key, fallback) {
      try {
        const v = localStorage.getItem("usbToggle." + key);
        return v == null ? fallback : JSON.parse(v);
      } catch {
        return fallback;
      }
    },
    set(key, value) {
      try {
        localStorage.setItem("usbToggle." + key, JSON.stringify(value));
      } catch {
        /* not important */
      }
    },
  };

  // ----------------------------------------------------------- helpers --

  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  function h(tag, attrs = {}, ...children) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k === "class") node.className = v;
      else if (k === "text") node.textContent = v;
      else if (k === "html") node.innerHTML = v;
      else if (k.startsWith("on")) node.addEventListener(k.slice(2), v);
      else node.setAttribute(k, v === true ? "" : v);
    }
    for (const c of children.flat()) {
      if (c == null || c === false) continue;
      node.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
    return node;
  }

  const iconSpan = (name, cls) => h("span", { class: cls, "data-icon": name, html: window.icon(name) });

  function hydrateIcons(root = document) {
    for (const el of $$("[data-icon]", root)) {
      if (!el.firstChild) el.innerHTML = window.icon(el.dataset.icon);
    }
  }

  function ago(ms) {
    if (!ms) return null;
    const diff = (Date.now() - ms) / 1000;
    const rtf = new Intl.RelativeTimeFormat(undefined, { numeric: "auto" });
    const steps = [
      [60, "second"],
      [3600, "minute", 60],
      [86400, "hour", 3600],
      [604800, "day", 86400],
      [2629800, "week", 604800],
      [31557600, "month", 2629800],
      [Infinity, "year", 31557600],
    ];
    if (diff < 45) return "just now";
    for (const [limit, unit, div = 1] of steps) {
      if (diff < limit) return rtf.format(-Math.round(diff / div), unit);
    }
    return null;
  }

  // Connected and not working. Disabled (code 22) is the user's choice, not a problem.
  const hasProblem = (d) => d.present && !!d.problem && d.problem !== 22;
  const isOn = (d) => (state.pending.has(d.id) ? state.pending.get(d.id) : d.enabled);
  const isBusy = (d) => state.pending.has(d.id) || state.busy.has(d.id);
  const categoryLabel = (d) => CATEGORY[d.category] || CATEGORY.other;
  const vidPid = (d) => (d.vid && d.pid ? `${d.vid}:${d.pid}` : null);

  function hubChildren(hub) {
    return state.devices.filter((d) => d.hubId === hub.id && d.present);
  }

  // ------------------------------------------------------------ toasts --

  function toast(message, { kind = "info", action, duration } = {}) {
    const host = $("#toasts");
    const iconName = kind === "error" ? "alert" : kind === "success" ? "check" : "info";
    const close = () => {
      if (!node.isConnected || node.classList.contains("leaving")) return;
      node.classList.add("leaving");
      setTimeout(() => node.remove(), 180);
    };
    const node = h(
      "div",
      { class: `toast ${kind}`, role: kind === "error" ? "alert" : "status" },
      iconSpan(iconName),
      h("div", { class: "toast-msg", text: message }),
      action &&
        h("button", {
          class: "toast-action",
          type: "button",
          text: action.label,
          onclick: () => {
            close();
            action.run();
          },
        }),
      h("button", { class: "icon-btn toast-close", type: "button", "aria-label": "Dismiss", onclick: close }, iconSpan("x"))
    );
    host.append(node);
    while (host.children.length > 3) host.firstElementChild.remove();
    setTimeout(close, duration ?? (kind === "error" ? 9000 : 4000));
    return close;
  }

  function errorToast(message, code) {
    const action =
      code === "accessDenied" && state.info && !state.info.elevated
        ? { label: "Restart as admin", run: restartAsAdmin }
        : null;
    toast(message || "Something went wrong.", { kind: "error", action });
  }

  // ------------------------------------------------------- data intake --

  function applySnapshot(snap) {
    if (!snap) return;
    state.loaded = true;
    state.devices = snap.devices || [];
    state.byId = new Map(state.devices.map((d) => [d.id, d]));
    state.busy = new Set(snap.busy || []);
    state.scannedAt = snap.scannedAt || 0;
    state.scanError = snap.error || null;
    for (const id of state.expanded) if (!state.byId.has(id)) state.expanded.delete(id);
    render();
  }

  function applyTheme(theme) {
    if (theme === "light" || theme === "dark") document.documentElement.dataset.theme = theme;
    else delete document.documentElement.dataset.theme;
    store.set("theme", theme);
  }

  function applyInfo(info) {
    state.info = info;
    state.settings = { ...DEFAULT_SETTINGS, ...(info.settings || {}) };
    applyTheme(state.settings.theme);
    $("#banner").hidden = !!info.elevated;
    $("#version").textContent = info.version ? `v${info.version}${info.portable ? " (portable)" : ""}` : "";
    syncSettingsDialog();
    const notice = $("#notice");
    notice.hidden = !info.configError;
    notice.textContent = info.configError || "";
    render();
  }

  // Saves settings right away (used outside the Settings dialog, e.g. the
  // "Show them" notes). Returns true on success.
  async function saveSettings(patch) {
    const before = state.settings;
    state.settings = { ...state.settings, ...patch };
    render();
    syncSettingsDialog();
    try {
      state.settings = await invoke("save_settings", { settings: state.settings });
      return true;
    } catch (e) {
      state.settings = before;
      render();
      syncSettingsDialog();
      errorToast(String(e));
      return false;
    }
  }

  // ----------------------------------------------------------- actions --

  async function refresh() {
    if (state.refreshing) return;
    state.refreshing = true;
    const btn = $("#refresh");
    btn.classList.add("spinning");
    const started = Date.now();
    try {
      applySnapshot(await invoke("get_devices", { force: true }));
    } catch (e) {
      errorToast(String(e));
    } finally {
      // Keep the spin visible long enough to register.
      setTimeout(() => {
        btn.classList.remove("spinning");
        state.refreshing = false;
      }, Math.max(0, 450 - (Date.now() - started)));
    }
  }

  async function setEnabled(id, enable, { confirm = true, undo = true } = {}) {
    const d = state.byId.get(id);
    if (!d || isBusy(d)) return;
    if (!enable && d.locked) {
      toast(`${d.name} is protected. Unprotect it with the lock button to turn it off.`, { kind: "info" });
      return;
    }

    if (!enable && confirm && state.settings.confirmRisky && RISKY.has(d.category) && d.present) {
      if (!(await confirmDisable(d))) return;
    }

    state.pending.set(id, enable);
    render();
    try {
      const r = await invoke("set_device_enabled", { id, enabled: enable });
      state.pending.delete(id);
      applySnapshot(r.snapshot);
      if (!r.ok) {
        errorToast(r.message, r.code);
      } else if (r.needsRestart) {
        toast(r.message || "Restart Windows to finish this change.", { kind: "info", duration: 7000 });
      } else {
        const now = state.byId.get(id) || d;
        const verb = enable ? "on" : "off";
        const where = now.present ? "" : " It will stay " + verb + " when you plug it in.";
        toast(`Turned ${verb} ${now.name}.${where}`, {
          kind: "success",
          action: undo ? { label: "Undo", run: () => setEnabled(id, !enable, { confirm: false, undo: false }) } : null,
        });
      }
    } catch (e) {
      state.pending.delete(id);
      render();
      errorToast(String(e));
    }
  }

  async function setPinned(id, pinned) {
    try {
      applySnapshot(await invoke("set_pinned", { id, pinned }));
      if (pinned && !store.get("pinHintShown", false)) {
        store.set("pinHintShown", true);
        toast("Pinned. You can also switch it from the tray icon menu.", { kind: "info", duration: 5000 });
      }
    } catch (e) {
      errorToast(String(e));
    }
  }

  async function setIcon(id, icon) {
    try {
      applySnapshot(await invoke("set_icon", { id, icon }));
    } catch (e) {
      errorToast(String(e));
    }
  }

  async function setLocked(id, locked) {
    const d = state.byId.get(id);
    const name = d ? d.name : "Device";
    if (!locked) {
      // A protected device stays protected while "Protect devices" is on.
      if (state.settings.protectInput) {
        toast(`${name} is protected. To unprotect devices, turn off "Protect devices" in Settings first.`, { kind: "info", duration: 6000 });
        return;
      }
      const ok = await askConfirm({
        title: `Unprotect ${name}?`,
        text: "Once unprotected, this device can be turned off with one click. If it is a keyboard or mouse you rely on, you could lock yourself out of the PC until you turn it back on.",
        okLabel: "Unprotect",
      });
      if (!ok) return;
    }
    try {
      applySnapshot(await invoke("set_locked", { id, locked }));
      toast(
        locked
          ? `${name} is protected. It can't be turned off or unprotected while "Protect devices" is on.`
          : `${name} is no longer protected.`,
        { kind: "info" }
      );
    } catch (e) {
      errorToast(String(e));
    }
  }

  async function rename(id, name) {
    try {
      applySnapshot(await invoke("rename_device", { id, name: name && name.trim() ? name.trim() : null }));
    } catch (e) {
      errorToast(String(e));
    }
  }

  async function restartAsAdmin() {
    try {
      await invoke("restart_as_admin");
    } catch (e) {
      toast(String(e), { kind: "info" });
    }
  }

  async function copy(text) {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const ta = h("textarea", { style: "position:fixed;opacity:0" });
      ta.value = text;
      document.body.append(ta);
      ta.select();
      document.execCommand("copy");
      ta.remove();
    }
    toast("Copied to clipboard.", { kind: "success", duration: 2000 });
  }

  // ------------------------------------------------------- confirmation --

  // Warning dialog; resolves true when the confirm button is pressed.
  // `dontAsk` adds a "Don't ask again" box that turns off "Confirm risky changes".
  function askConfirm({ title, text, okLabel = "Turn off", dontAsk = false }) {
    const dialog = $("#confirm");
    $("#confirm-title").textContent = title;
    $("#confirm-text").textContent = text || "";
    $("#confirm-ok").textContent = okLabel;
    $("#confirm-dont-ask").checked = false;
    $("#confirm-dont-ask-row").hidden = !dontAsk;
    dialog.returnValue = "";
    dialog.showModal();
    $("#confirm-ok").focus();
    return new Promise((resolve) => {
      dialog.addEventListener(
        "close",
        () => {
          const ok = dialog.returnValue === "ok";
          if (ok && dontAsk && $("#confirm-dont-ask").checked) saveSettings({ confirmRisky: false });
          resolve(ok);
        },
        { once: true }
      );
    });
  }

  function confirmDisable(d) {
    const kids = d.category === "hub" ? hubChildren(d).length : 0;
    const text = {
      keyboard: "If this is your only keyboard you won't be able to type until it's turned back on. You can still use the mouse to turn it back on here.",
      mouse: "If this is your only mouse, use the keyboard to turn it back on: press Tab to reach its switch, then Space.",
      input: "Keyboards and mice connected through this device will stop working until it's turned back on.",
      bluetooth: "Bluetooth keyboards, mice, headphones and other wireless devices will disconnect.",
      hub:
        (kids
          ? `The ${kids} device${kids === 1 ? "" : "s"} plugged into this hub will stop working too`
          : "Everything plugged into this hub will stop working too") + ", including any keyboard or mouse.",
    }[d.category];
    return askConfirm({ title: `Turn off ${d.name}?`, text: text || "", okLabel: "Turn off", dontAsk: true });
  }

  // --------------------------------------------------------- rendering --

  const rows = new Map(); // id -> { root, refs }
  const sections = new Map(); // key -> { root, head, cards }

  function matches(d, q) {
    if (!q) return true;
    const hay = [d.name, d.defaultName, d.description, d.vendor, categoryLabel(d), vidPid(d), d.id, d.hubName, panelLabel(d), ...(d.functions || [])]
      .filter(Boolean)
      .join("\n")
      .toLowerCase();
    return q
      .split(/\s+/)
      .filter(Boolean)
      .every((term) => hay.includes(term));
  }

  function passesFilter(d) {
    if (state.filter === "on") return isOn(d);
    if (state.filter === "off") return !isOn(d);
    return true;
  }

  function metaParts(d, hidePanel = false) {
    const parts = [];
    const on = isOn(d);
    if (isBusy(d)) {
      parts.push(h("span", { class: "state-busy", text: state.pending.has(d.id) ? (on ? "Turning on…" : "Turning off…") : "Working…" }));
    } else if (!d.present) {
      if (!d.enabled) parts.push(h("span", { class: "state-off", text: "Off" }));
      const seen = ago(d.lastSeen);
      parts.push(seen ? `Not connected · seen ${seen}` : "Not connected");
    } else if (!d.enabled) {
      parts.push(h("span", { class: "state-off", text: "Off" }));
    } else if (d.problem) {
      parts.push(h("span", { class: "state-problem", text: `Problem (code ${d.problem})` }));
    }

    parts.push(categoryLabel(d));
    if (d.vendor && !d.name.toLowerCase().includes(d.vendor.split(" ")[0].toLowerCase())) parts.push(d.vendor);
    if (d.builtIn) parts.push(h("span", { class: "built-in-tag", text: "Built-in" }));
    else if (d.present) {
      if (panelLabel(d) && !hidePanel) parts.push(panelLabel(d));
      if (d.hubName) parts.push(`via ${d.hubName}`);
      else if (d.port) parts.push(`Port ${d.port}`);
    }
    if (d.isHub && d.present) {
      const n = hubChildren(d).length;
      parts.push(`${n} device${n === 1 ? "" : "s"}`);
    }

    const out = [];
    parts.forEach((p, i) => {
      if (i) out.push(" · ");
      out.push(p);
    });
    return out;
  }

  function createRow(d) {
    const refs = {};
    refs.tile = h("span", { class: "tile" });
    refs.label = h("span", { class: "label" });
    refs.pinMark = iconSpan("pin", "pin-mark");
    refs.lockMark = iconSpan("lock", "lock-mark");
    refs.meta = h("span", { class: "device-meta" });
    refs.main = h(
      "button",
      { class: "device-main", type: "button", "aria-expanded": "false", onclick: () => toggleExpanded(d.id) },
      refs.tile,
      h("span", { class: "device-text" }, h("span", { class: "device-name" }, refs.label, refs.pinMark, refs.lockMark), refs.meta)
    );
    refs.lock = h(
      "button",
      { class: "icon-btn lock-btn", type: "button", onclick: () => setLocked(d.id, !state.byId.get(d.id)?.locked) },
      iconSpan("lock")
    );
    refs.pin = h(
      "button",
      { class: "icon-btn pin-btn", type: "button", onclick: () => setPinned(d.id, !state.byId.get(d.id)?.pinned) },
      iconSpan("pin")
    );
    refs.expand = h(
      "button",
      { class: "icon-btn expand-btn", type: "button", tabindex: "-1", "aria-hidden": "true", onclick: () => toggleExpanded(d.id) },
      iconSpan("chevron")
    );
    refs.sw = h(
      "button",
      {
        class: "switch",
        type: "button",
        role: "switch",
        onclick: () => {
          const cur = state.byId.get(d.id);
          if (cur) setEnabled(cur.id, !isOn(cur));
        },
      },
      h("span", { class: "knob" })
    );
    refs.details = h("div", { class: "details" }, h("div", { class: "details-inner" }));
    const root = h(
      "article",
      { class: "device", "data-id": d.id },
      h("div", { class: "device-row" }, refs.main, h("div", { class: "row-actions" }, refs.lock, refs.pin, refs.expand), refs.sw),
      refs.details
    );
    const row = { root, refs, iconKey: null };
    rows.set(d.id, row);
    return row;
  }

  function updateRow(row, d) {
    const { root, refs } = row;
    const on = isOn(d);
    const busy = isBusy(d);
    const expanded = state.expanded.has(d.id);

    root.classList.toggle("off", !on);
    root.classList.toggle("missing", !d.present);
    root.classList.toggle("expanded", expanded);

    const key = iconKey(d);
    if (row.iconKey !== key) {
      row.iconKey = key;
      refs.tile.className = `tile cat-${colorOf(key)}`;
      refs.tile.replaceChildren(iconSpan(key));
    }
    refs.label.textContent = d.name;
    refs.label.title = d.name;
    refs.pinMark.hidden = !d.pinned;
    refs.lockMark.hidden = !d.locked;
    refs.lockMark.title = "Protected";
    refs.lock.setAttribute("aria-pressed", String(!!d.locked));
    // While "Protect devices" is on, a protected device can't be unprotected.
    const fixed = !!d.locked && !!state.settings.protectInput;
    refs.lock.classList.toggle("fixed", fixed);
    refs.lock.setAttribute("aria-disabled", String(fixed));
    refs.lock.title = fixed
      ? 'Protected. Turn off "Protect devices" in Settings to unprotect.'
      : d.locked
        ? "Unprotect (allow turning off)"
        : "Protect (can't be turned off)";
    refs.lock.setAttribute("aria-label", refs.lock.title);
    refs.lock.replaceChildren(iconSpan(d.locked ? "lock" : "unlock"));
    refs.meta.replaceChildren(...metaParts(d, row.inPanelSection));
    refs.main.setAttribute("aria-expanded", String(expanded));

    refs.pin.setAttribute("aria-pressed", String(!!d.pinned));
    refs.pin.title = d.pinned ? "Unpin" : "Pin to top and tray menu";
    refs.pin.setAttribute("aria-label", refs.pin.title);
    refs.expand.title = expanded ? "Hide details" : "Details";

    refs.sw.setAttribute("aria-checked", String(on));
    refs.sw.setAttribute("aria-busy", String(busy));
    const action = `${on ? "Turn off" : "Turn on"} ${d.name}`;
    // Protected and on: visible but frozen (turning it on stays possible).
    const frozen = !!d.locked && on;
    refs.sw.classList.toggle("frozen", frozen);
    refs.sw.setAttribute("aria-disabled", String(frozen));
    refs.sw.title = busy
      ? "Working…"
      : frozen
        ? "Protected — unprotect it to turn it off"
        : on
          ? "On — click to turn off"
          : "Off — click to turn on";
    refs.sw.setAttribute("aria-label", action);

    if (expanded) renderDetails(row, d);
  }

  function fact(label, value, opts = {}) {
    if (value == null || value === "") return [];
    const dd = h("dd", { class: opts.mono ? "mono" : null });
    if (opts.copy) {
      dd.append(
        h(
          "span",
          { class: "copy-line" },
          h("span", { text: value }),
          h(
            "button",
            { class: "icon-btn copy-btn", type: "button", title: "Copy", "aria-label": `Copy ${label}`, onclick: () => copy(value) },
            iconSpan("copy")
          )
        )
      );
    } else {
      dd.textContent = value;
    }
    return [h("dt", { text: label }), dd];
  }

  function renderDetails(row, d) {
    const inner = row.refs.details.firstElementChild;
    let content = inner.firstElementChild;
    if (!content) {
      const input = h("input", {
        type: "text",
        maxlength: "80",
        spellcheck: "false",
        "aria-label": "Device name",
        onkeydown: (e) => {
          if (e.key === "Enter") {
            e.preventDefault();
            save.click();
          } else if (e.key === "Escape") {
            e.stopPropagation();
            input.value = state.byId.get(d.id)?.customName || "";
            input.blur();
          }
        },
        oninput: () => syncRenameButtons(),
      });
      const save = h("button", {
        class: "btn btn-small btn-primary",
        type: "button",
        text: "Save name",
        onclick: async () => {
          await rename(d.id, input.value);
          input.blur();
          syncRenameButtons();
        },
      });
      const reset = h("button", {
        class: "btn btn-small",
        type: "button",
        text: "Use default",
        onclick: async () => {
          input.value = "";
          await rename(d.id, null);
          syncRenameButtons();
        },
      });
      const syncRenameButtons = () => {
        const cur = state.byId.get(d.id);
        const typed = input.value.trim();
        save.disabled = typed === (cur?.customName || "") || (!cur?.customName && !typed);
        reset.hidden = !cur?.customName;
      };
      const picker = h("div", { class: "icon-picker", role: "group", "aria-label": "Icon" });
      content = h(
        "div",
        { class: "details-content" },
        h("div", { class: "problem-slot" }),
        h("div", { class: "rename" }, input, save, reset),
        h("p", { class: "picker-label", text: "Icon" }),
        picker,
        h("dl", { class: "facts" })
      );
      content._rename = { input, syncRenameButtons };
      content._picker = picker;
      inner.append(content);
    }

    const { input, syncRenameButtons } = content._rename;
    if (document.activeElement !== input) input.value = d.customName || "";
    input.placeholder = d.defaultName;
    syncRenameButtons();

    renderIconPicker(content._picker, d);

    const slot = $(".problem-slot", content);
    const problemText = d.present && d.problem && d.problem !== 22 ? PROBLEMS[d.problem] || `Windows reports problem code ${d.problem}.` : null;
    slot.replaceChildren(problemText ? h("div", { class: "problem-note" }, iconSpan("alert"), h("span", { text: problemText })) : "");

    const status = isBusy(d)
      ? "Working…"
      : !d.present
        ? d.enabled
          ? "Not connected"
          : "Off · not connected (stays off when plugged in)"
        : !d.enabled
          ? "Off"
          : d.problem
            ? `On, with a problem (code ${d.problem})`
            : "On and working";

    const seen = d.lastSeen ? new Date(d.lastSeen).toLocaleString() : null;
    $(".facts", content).replaceChildren(
      ...fact("Status", status),
      ...fact("Type", categoryLabel(d)),
      ...fact("Protection", protectionText(d)),
      ...fact("Windows name", d.description && d.description !== d.name ? d.description : null),
      ...fact("Manufacturer", d.vendor),
      ...fact("Hardware ID", vidPid(d), { mono: true }),
      ...fact("Connection", connectionText(d)),
      ...fact(d.present ? "Connected since" : "Last seen", seen),
      ...fact("Includes", d.functions && d.functions.length ? d.functions.join(", ") : null),
      ...fact("Instance ID", d.id, { mono: true, copy: true })
    );
  }

  function iconButton(key, label, onPick, extraClass = "") {
    return h(
      "button",
      {
        class: `icon-choice cat-${colorOf(key)} ${extraClass}`.trim(),
        type: "button",
        title: label,
        "aria-label": label,
        "data-key": key,
        onclick: onPick,
      },
      iconSpan(key)
    );
  }

  // Quick row (Auto + common icons) and an expandable, searchable library.
  function renderIconPicker(picker, d) {
    const auto = autoIconKey(d);
    const current = knownIcon(d.customIcon) ? d.customIcon : null;
    const stamp = `${auto}|${current}`;
    if (picker.dataset.stamp === stamp) return;
    picker.dataset.stamp = stamp;

    const autoBtn = iconButton(auto, `Automatic (${CATEGORY[auto] || "USB device"})`, () => setIcon(d.id, null), "auto");
    autoBtn.append("Auto");
    autoBtn.setAttribute("aria-pressed", String(!current));

    const quickKeys = ICON_CHOICES.map(([k]) => k);
    if (current && !quickKeys.includes(current)) quickKeys.unshift(current); // keep a library pick visible
    const quick = quickKeys.map((key) => {
      const b = iconButton(key, iconLabel(key), () => setIcon(d.id, key));
      b.setAttribute("aria-pressed", String(current === key));
      return b;
    });

    if (!picker._library) picker._library = buildIconLibrary(d.id);
    picker._library.sync(current);
    picker.replaceChildren(h("div", { class: "icon-quick" }, autoBtn, ...quick), picker._library.root);
  }

  function buildIconLibrary(id) {
    const lib = window.ICON_LIBRARY;
    const root = h("div", { class: "icon-more" });
    if (!lib) return { root, sync() {} };

    const toggle = h("button", { class: "link-btn more-toggle", type: "button", "aria-expanded": "false" });
    const search = h("input", {
      class: "icon-search",
      type: "text",
      placeholder: `Search ${lib.count} icons`,
      "aria-label": "Search icons",
      spellcheck: "false",
      autocomplete: "off",
    });
    const empty = h("p", { class: "icon-empty", text: "No icons match.", hidden: true });
    const list = h("div", { class: "icon-groups" });
    const panel = h("div", { class: "icon-library", hidden: true }, search, list, empty);
    const buttons = [];
    let selected = null;

    const build = () => {
      for (const g of lib.groups) {
        const grid = h("div", { class: "icon-grid" });
        for (const ic of g.icons) {
          const b = iconButton(ic.key, iconLabel(ic.key), () => setIcon(id, ic.key));
          b.dataset.search = `${ic.name.replace(/-/g, " ")} ${ic.keywords} ${g.label}`.toLowerCase();
          b.setAttribute("aria-pressed", String(selected === ic.key));
          buttons.push(b);
          grid.append(b);
        }
        list.append(h("div", { class: "icon-group" }, h("p", { class: "icon-group-label", text: g.label }), grid));
      }
    };

    const filter = () => {
      const terms = search.value.trim().toLowerCase().split(/\s+/).filter(Boolean);
      let shown = 0;
      for (const b of buttons) {
        const ok = terms.every((t) => b.dataset.search.includes(t));
        b.hidden = !ok;
        if (ok) shown++;
      }
      for (const g of list.children) g.hidden = !g.querySelector(".icon-choice:not([hidden])");
      empty.hidden = shown > 0;
    };

    const setOpen = (open) => {
      if (open && !buttons.length) build();
      panel.hidden = !open;
      toggle.setAttribute("aria-expanded", String(open));
      toggle.textContent = open ? "Fewer icons" : `More icons (${lib.count})`;
      if (open) search.focus();
    };

    toggle.addEventListener("click", () => setOpen(panel.hidden));
    search.addEventListener("input", filter);
    search.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && search.value) {
        e.preventDefault();
        e.stopPropagation();
        search.value = "";
        filter();
      } else if (e.key === "Enter") {
        const first = buttons.find((b) => !b.hidden);
        if (first) first.click();
      }
    });
    setOpen(false);
    root.append(toggle, panel);

    return {
      root,
      sync(current) {
        selected = current;
        for (const b of buttons) b.setAttribute("aria-pressed", String(b.dataset.key === current));
      },
    };
  }

  function protectionText(d) {
    if (d.autoLocked) return "Protected automatically (keyboards and mice)";
    if (d.locked) return "Protected by you";
    return null;
  }

  function connectionText(d) {
    if (d.builtIn) return "Built into this PC (internal USB)";
    if (!d.present) return null;
    const port = d.port ? `port ${d.port}` : null;
    const side = panelLabel(d);
    const where = side ? ` (${side.toLowerCase()})` : "";
    if (d.hubName) return `USB hub “${d.hubName}”${port ? `, ${port}` : ""}${where}`;
    return (port ? `USB ${port} on this PC` : "USB port on this PC") + where;
  }

  function toggleExpanded(id) {
    if (state.expanded.has(id)) state.expanded.delete(id);
    else state.expanded.add(id);
    const row = rows.get(id);
    const d = state.byId.get(id);
    if (row && d) updateRow(row, d);
  }

  function getSection(key, title, collapsible) {
    let s = sections.get(key);
    if (!s) {
      const count = h("span", { class: "count" });
      const extra = h("span", { class: "extra" });
      const head = collapsible
        ? h(
            "button",
            {
              class: "section-head",
              type: "button",
              onclick: () => {
                state.collapsed[key] = !state.collapsed[key];
                store.set("collapsed", state.collapsed);
                render();
              },
            },
            h("span", { text: title }),
            count,
            extra,
            iconSpan("chevron", "chev")
          )
        : h("h2", { class: "section-head" }, h("span", { text: title }), count, extra);
      const cards = h("div", { class: "cards" });
      const root = h("section", { class: "section", "data-section": key }, head, cards);
      s = { root, head, cards, count, extra, collapsible };
      sections.set(key, s);
    }
    return s;
  }

  // Notes at the end of the list about devices hidden by a setting, each
  // with a button that turns the setting off.
  const hiddenNotes = new Map();
  function getHiddenNote(key, settingsPatch) {
    let note = hiddenNotes.get(key);
    if (!note) {
      const text = h("span");
      const root = h(
        "div",
        { class: "hidden-note" },
        text,
        h("button", { class: "link-btn", type: "button", text: "Show them", onclick: () => saveSettings(settingsPatch) })
      );
      note = { root, text };
      hiddenNotes.set(key, note);
    }
    return note;
  }

  // "<lead>: A, B, C and 2 more."
  function describeHidden(list, lead) {
    const present = list.filter((d) => d.present);
    const names = (present.length ? present : list).slice(0, 3).map((d) => d.name);
    const more = list.length - names.length;
    return `${lead(list.length)}: ${names.join(", ")}${more > 0 ? ` and ${more} more` : ""}.`;
  }

  function reconcile(container, nodes) {
    const wanted = new Set(nodes);
    for (const child of Array.from(container.children)) if (!wanted.has(child)) child.remove();
    nodes.forEach((node, i) => {
      if (container.children[i] !== node) container.insertBefore(node, container.children[i] || null);
    });
  }

  function skeleton() {
    const line = (w, hgt = 10) => h("span", { class: "sk", style: `width:${w};height:${hgt}px` });
    const row = (w) =>
      h(
        "div",
        { class: "skeleton" },
        h("span", { class: "sk", style: "width:38px;height:38px;border-radius:10px" }),
        h("span", { style: "display:flex;flex-direction:column;gap:7px;flex:1" }, line(w, 12), line("35%")),
        h("span", { class: "sk", style: "width:42px;height:22px;border-radius:11px" })
      );
    return h("div", { class: "cards" }, row("48%"), row("62%"), row("40%"), row("55%"));
  }

  function emptyState(iconName, title, text, button) {
    return h(
      "div",
      { class: "empty" },
      iconSpan(iconName, "empty-icon"),
      h("strong", { text: title }),
      text && h("span", { text }),
      button && h("button", { class: "btn", type: "button", text: button.label, onclick: button.run })
    );
  }

  function renderSummary() {
    const el = $("#summary");
    if (!state.loaded) {
      el.textContent = "Looking for devices…";
      return;
    }
    const s = state.settings;
    const connected = state.devices.filter(
      (d) =>
        d.present &&
        (s.showHubs || !d.isHub) &&
        (d.pinned || ((!s.portsOnly || !d.builtIn) && (!s.hideProblems || !hasProblem(d))))
    );
    const off = connected.filter((d) => !d.enabled).length;
    const parts = [`${connected.length} connected`];
    if (off) parts.push(`${off} off`);
    el.textContent = parts.join(" · ");
    el.title = state.scannedAt ? `Updated ${new Date(state.scannedAt).toLocaleTimeString()}` : "";
  }

  function render() {
    renderSummary();
    const list = $("#list");

    if (!state.loaded) {
      reconcile(list, [list.querySelector(".cards.sk-wrap") || Object.assign(skeleton(), { className: "cards sk-wrap" })]);
      return;
    }

    const q = state.query.trim().toLowerCase();
    const s = state.settings;
    const hiddenBuiltIn = [];
    const hiddenProblems = [];
    const visible = state.devices.filter((d) => {
      if (!matches(d, q) || !passesFilter(d)) return false;
      if (d.pinned) return true;
      if (d.isHub && !s.showHubs && !q) return false;
      if (!d.present && !s.showDisconnected && d.enabled && !q) return false;
      if (d.builtIn && s.portsOnly) {
        hiddenBuiltIn.push(d);
        return false;
      }
      if (s.hideProblems && hasProblem(d)) {
        hiddenProblems.push(d);
        return false;
      }
      return true;
    });

    const pinned = visible.filter((d) => d.pinned);
    const connected = visible.filter((d) => !d.pinned && d.present && !d.isHub);
    const hubs = visible.filter((d) => !d.pinned && d.present && d.isHub);
    const gone = visible
      .filter((d) => !d.pinned && !d.present)
      .sort((a, b) => Number(a.enabled) - Number(b.enabled) || (b.lastSeen || 0) - (a.lastSeen || 0));

    // Connected devices are grouped by where the port is on the case
    // (front, back, ...). Devices behind a hub follow the hub's port.
    // Protected devices get their own group; the rest by location.
    const protectedList = connected.filter((d) => d.locked);
    const external = connected.filter((d) => !d.locked && !d.builtIn);
    const byPanel = PANEL_ORDER.map((p) => [p, external.filter((d) => d.panel === p)]);
    const anyPanel = byPanel.some(([, items]) => items.length);
    const unplaced = external.filter((d) => !PANEL[d.panel]);
    const builtIn = connected.filter((d) => !d.locked && d.builtIn);

    const groups = [
      ["pinned", "Pinned", pinned, false],
      ...byPanel.map(([p, items]) => [`panel-${p}`, PANEL[p], items, false]),
      ["connected", anyPanel ? "Other ports" : "Connected", unplaced, false],
      ["builtin", "Built into this PC", builtIn, false],
      ["protected", "Protected", protectedList, false],
      ["hubs", "USB hubs", hubs, true],
      ["disconnected", "Not connected", gone, true],
    ];

    const nodes = [];
    for (const [key, title, items, collapsible] of groups) {
      if (!items.length) continue;
      const sec = getSection(key, title, collapsible);
      const collapsed = collapsible && state.collapsed[key] && !q;
      sec.root.classList.toggle("collapsed", !!collapsed);
      if (collapsible) sec.head.setAttribute("aria-expanded", String(!collapsed));
      sec.count.textContent = items.length;
      const offCount = key === "disconnected" ? items.filter((d) => !d.enabled).length : 0;
      sec.extra.textContent = offCount ? `${offCount} off` : "";
      sec.extra.hidden = !offCount;
      if (!collapsed) {
        reconcile(
          sec.cards,
          items.map((d) => {
            const row = rows.get(d.id) || createRow(d);
            // The section title already says front/back; don't repeat it.
            row.inPanelSection = key.startsWith("panel-");
            updateRow(row, d);
            return row.root;
          })
        );
      }
      nodes.push(sec.root);
    }

    // Forget rows for devices that disappeared entirely.
    for (const id of Array.from(rows.keys())) if (!state.byId.has(id)) rows.delete(id);

    if (!nodes.length) {
      if (state.scanError && !state.devices.length) {
        nodes.push(emptyState("alert", "Couldn't read USB devices", state.scanError, { label: "Try again", run: refresh }));
      } else if (q || state.filter !== "all") {
        const what = q ? `No devices match “${state.query.trim()}”` : state.filter === "on" ? "No devices are on" : "No devices are off";
        nodes.push(
          emptyState("search", what, null, {
            label: "Show all devices",
            run: () => {
              setQuery("");
              setFilter("all");
            },
          })
        );
      } else {
        const someHidden = hiddenBuiltIn.length + hiddenProblems.length > 0;
        const text = someHidden
          ? "Nothing working is plugged into a USB port right now."
          : "Plug something in. The list updates by itself.";
        nodes.push(emptyState("usb", "No USB devices found", text, someHidden ? null : { label: "Refresh", run: refresh }));
      }
    }

    if (hiddenBuiltIn.length) {
      const note = getHiddenNote("builtin", { portsOnly: false });
      note.text.textContent = describeHidden(hiddenBuiltIn, (n) =>
        n === 1 ? "1 built-in device is hidden" : `${n} built-in devices are hidden`
      );
      nodes.push(note.root);
    }
    if (hiddenProblems.length) {
      const note = getHiddenNote("problems", { hideProblems: false });
      note.text.textContent = describeHidden(hiddenProblems, (n) =>
        n === 1 ? "1 device isn't working and is hidden" : `${n} devices aren't working and are hidden`
      );
      nodes.push(note.root);
    }

    reconcile(list, nodes);
  }

  // ---------------------------------------------------- filter & search --

  function setFilter(f) {
    state.filter = f;
    for (const b of $$("#filter button")) b.setAttribute("aria-checked", String(b.dataset.filter === f));
    render();
  }

  function setQuery(q) {
    state.query = q;
    const input = $("#search");
    if (input.value !== q) input.value = q;
    $("#search-clear").hidden = !q;
    render();
  }

  // ----------------------------------------------------------- settings --

  // The Settings dialog edits a draft; nothing is applied until Save.
  let draft = null; // { settings, autostart }
  const savedAutostart = () => !!state.info?.autostart?.enabled;

  function settingsDirty() {
    if (!draft) return false;
    const keys = new Set([...Object.keys(draft.settings), ...Object.keys(state.settings)]);
    for (const k of keys) if (draft.settings[k] !== state.settings[k]) return true;
    return draft.autostart !== savedAutostart();
  }

  function syncSettingsDialog() {
    const s = draft ? draft.settings : state.settings;
    const autoOn = draft ? draft.autostart : savedAutostart();
    for (const b of $$("#theme button")) b.setAttribute("aria-checked", String(b.dataset.themeValue === s.theme));
    for (const sw of $$("[data-setting]")) sw.setAttribute("aria-checked", String(!!s[sw.dataset.setting]));

    const info = state.info;
    const saved = info?.autostart || { enabled: false, elevated: false };
    $("#autostart").setAttribute("aria-checked", String(autoOn));
    let desc;
    if (autoOn && saved.enabled && saved.elevated) desc = "Starts with administrator rights, without a UAC prompt.";
    else if (autoOn && saved.enabled)
      desc = info?.elevated
        ? "Starts without administrator rights. Turn this off, save, then on again to start as admin."
        : "Starts without administrator rights.";
    else if (autoOn)
      desc = info?.elevated ? "Will start with administrator rights, without a UAC prompt." : "Will start without administrator rights.";
    else if (info?.elevated) desc = "Starts with administrator rights, without a UAC prompt.";
    else desc = "Restart as admin first so it can start with administrator rights.";
    $("#autostart-desc").textContent = desc;

    const minSw = $('[data-setting="startMinimized"]');
    minSw.disabled = !autoOn;
    minSw.closest(".setting").classList.toggle("disabled", !autoOn);

    const dirty = settingsDirty();
    $("#settings-save").disabled = !dirty;
    $("#settings-dirty").hidden = !dirty;
  }

  async function saveDraft() {
    if (!draft) return;
    const saveBtn = $("#settings-save");
    saveBtn.disabled = true;
    saveBtn.textContent = "Saving…";
    const wantAutostart = draft.autostart;
    let ok = await saveSettings({ ...draft.settings });
    if (ok && wantAutostart !== savedAutostart()) {
      try {
        state.info.autostart = await invoke("set_autostart", { enabled: wantAutostart });
      } catch (e) {
        ok = false;
        errorToast(String(e));
      }
    }
    saveBtn.textContent = "Save";
    if (ok) {
      draft = null;
      $("#settings").close();
      toast("Settings saved.", { kind: "success", duration: 2500 });
    } else {
      syncSettingsDialog();
    }
  }

  function discardDraft() {
    draft = null;
    applyTheme(state.settings.theme); // undo the theme preview
    $("#settings").close();
  }

  // Every way of leaving Settings comes here.
  function requestCloseSettings() {
    if (settingsDirty()) {
      const dlg = $("#unsaved");
      dlg.returnValue = "";
      dlg.showModal();
      $("#unsaved-save").focus();
    } else {
      discardDraft();
    }
  }

  function openSettings() {
    draft = { settings: { ...state.settings }, autostart: savedAutostart() };
    syncSettingsDialog();
    $("#settings").showModal();
  }

  // -------------------------------------------------------------- wiring --

  function bind() {
    $("#refresh").addEventListener("click", refresh);
    $("#open-settings").addEventListener("click", openSettings);
    $("#elevate").addEventListener("click", restartAsAdmin);

    const search = $("#search");
    search.addEventListener("input", () => setQuery(search.value));
    search.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && search.value) {
        e.preventDefault();
        setQuery("");
      }
    });

    $("#search-clear").addEventListener("click", () => {
      setQuery("");
      search.focus();
    });

    for (const b of $$("#filter button")) b.addEventListener("click", () => setFilter(b.dataset.filter));

    const settings = $("#settings");
    settings.addEventListener("click", (e) => {
      if (e.target === settings) requestCloseSettings(); // backdrop click
    });
    settings.addEventListener("cancel", (e) => {
      e.preventDefault(); // Esc: ask first when there are unsaved changes
      requestCloseSettings();
    });
    $("[data-close]", settings).addEventListener("click", requestCloseSettings);
    $("#settings-cancel").addEventListener("click", requestCloseSettings);
    $("#settings-save").addEventListener("click", saveDraft);
    for (const b of $$("#theme button"))
      b.addEventListener("click", () => {
        if (!draft) return;
        draft.settings.theme = b.dataset.themeValue;
        applyTheme(b.dataset.themeValue); // live preview; reverted on Discard
        syncSettingsDialog();
      });
    for (const sw of $$("[data-setting]"))
      sw.addEventListener("click", () => {
        if (sw.disabled || !draft) return;
        const key = sw.dataset.setting;
        if (key === "protectInput" && draft.settings.protectInput) {
          // Turning protection off is the only way to unprotect devices: warn first.
          askConfirm({
            title: "Turn off device protection?",
            text: "Keyboards and mice will no longer be protected, and every protected device can be turned off or unprotected. If you turn off your only keyboard or mouse you could lock yourself out of the PC until you turn it back on.",
            okLabel: "Turn off protection",
          }).then((ok) => {
            if (!ok) return;
            draft.settings.protectInput = false;
            syncSettingsDialog();
          });
          return;
        }
        draft.settings[key] = !draft.settings[key];
        syncSettingsDialog();
      });
    $("#autostart").addEventListener("click", () => {
      if (!draft) return;
      draft.autostart = !draft.autostart;
      syncSettingsDialog();
    });

    const unsaved = $("#unsaved");
    unsaved.addEventListener("click", (e) => {
      if (e.target === unsaved) unsaved.close(""); // backdrop: keep editing
    });
    unsaved.addEventListener("close", () => {
      if (unsaved.returnValue === "save") saveDraft();
      else if (unsaved.returnValue === "discard") discardDraft();
      // Esc or backdrop: back to Settings, nothing lost.
    });
    for (const b of $$("[data-open-folder]"))
      b.addEventListener("click", () => invoke("open_folder", { which: b.dataset.openFolder }).catch((e) => errorToast(String(e))));
    $("#quit").addEventListener("click", () => invoke("quit"));

    const confirm = $("#confirm");
    confirm.addEventListener("click", (e) => {
      if (e.target === confirm) confirm.close("cancel");
    });

    document.addEventListener("keydown", (e) => {
      const inField = e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
      const dialogOpen = !!document.querySelector("dialog[open]");
      if ((e.ctrlKey && e.key.toLowerCase() === "f") || (e.key === "/" && !inField && !dialogOpen)) {
        e.preventDefault();
        if (!dialogOpen) {
          search.focus();
          search.select();
        }
      } else if (e.key === "F5" || (e.ctrlKey && e.key.toLowerCase() === "r")) {
        e.preventDefault(); // never reload the page
        if (!dialogOpen) refresh();
      } else if (e.ctrlKey && e.key.toLowerCase() === "s") {
        e.preventDefault();
        if ($("#settings").open && !$("#unsaved").open && settingsDirty()) saveDraft();
      } else if (e.ctrlKey && ["p", "u", "g", "j"].includes(e.key.toLowerCase())) {
        e.preventDefault();
      } else if (e.key === "Escape" && !inField && !dialogOpen && state.expanded.size) {
        const ids = Array.from(state.expanded);
        state.expanded.clear();
        ids.forEach((id) => {
          const row = rows.get(id);
          const d = state.byId.get(id);
          if (row && d) updateRow(row, d);
        });
      }
    });

    document.addEventListener("contextmenu", (e) => {
      const t = e.target;
      const editable = t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement;
      const hasSelection = String(window.getSelection() || "").length > 0;
      if (!editable && !hasSelection) e.preventDefault();
    });

    // Relative "seen 5 minutes ago" labels stay fresh.
    setInterval(() => {
      if (!document.hidden && state.loaded) render();
    }, 60_000);
  }

  async function init() {
    hydrateIcons();
    applyTheme(store.get("theme", "system"));
    state.collapsed = { ...state.collapsed, ...store.get("collapsed", {}) };
    bind();
    render();

    listen("devices", applySnapshot);
    listen("busy", (ids) => {
      state.busy = new Set(ids || []);
      render();
    });
    listen("toast", (t) => (t.kind === "error" ? errorToast(t.message, t.code) : toast(t.message, { kind: t.kind })));

    // Show the window once the first frame (with the right theme) is painted.
    requestAnimationFrame(() => requestAnimationFrame(() => invoke("ui_ready").catch(() => {})));

    invoke("get_info")
      .then(applyInfo)
      .catch((e) => errorToast(String(e)));
    try {
      applySnapshot(await invoke("get_devices", { force: false }));
    } catch (e) {
      state.loaded = true;
      state.scanError = String(e);
      render();
    }
  }

  if (!tauri) {
    document.body.textContent = "USB Toggle must be started as the desktop app.";
    return;
  }
  init();
})();
