// screenBlank - make screens look switched off, even when a monitor can't sleep.
//
// A tray app that covers monitors with pure black, topmost windows. On OLED a
// black pixel is an unlit pixel, and on LCD it is as dark as the panel gets
// without backlight control. Works per monitor, wakes on input (or only on a
// hotkey), can auto-blank on idle, and can additionally ask monitors to power
// down through Windows or DDC/CI.
//
// Build: build.bat (MSVC). Whole program is this file.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <powrprof.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "powrprof.lib")

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const wchar_t kAppName[]       = L"screenBlank";
static const wchar_t kVersion[]       = L"0.1.0";
static const wchar_t kMainClass[]     = L"screenBlank.Main";
static const wchar_t kOverlayClass[]  = L"screenBlank.Overlay";
static const wchar_t kIdentifyClass[] = L"screenBlank.Identify";
static const wchar_t kMutexName[]     = L"Local\\screenBlank.SingleInstance";
static const wchar_t kRunKey[]        = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const ULONG_PTR kCopyDataTag   = 0x53424C4B; // 'SBLK'

enum : UINT {
    WM_TRAY = WM_APP + 1,
    WM_APP_WAKE,
    WM_APP_CLOSE_OVERLAY,
    WM_APP_RUN_PENDING,
};


// How "Off" turns a screen off. Chosen per screen (by its EDID name) in the Screens window.
enum OffMethod : int {
    OFF_AUTO = 0,   // the screen's own standby (DDC/CI) if it has one, otherwise black
    OFF_CUT = 2,    // take the display off the desktop; windows are put back afterwards
    OFF_BLACK = 3,  // black overlay only, the monitor itself is never touched
    OFF_HARD = 4,   // DDC/CI power off (power light off too); some monitors then need their button
};

enum : UINT_PTR {
    TIMER_ANIM = 1,
    TIMER_TOPMOST,
    TIMER_IDLE,
    TIMER_CLOCK,
    TIMER_DISPLAY,
    TIMER_RESTORE_WINDOWS_1,
    TIMER_RESTORE_WINDOWS_2,
};

enum : int { HK_BLANK = 1, HK_OTHERS, HK_CURSOR, HK_DIM };

enum WakeMode : int { WAKE_ANY = 0, WAKE_KEYBOARD = 1, WAKE_HOTKEY = 2 };
enum OverlayMode : int { MODE_BLANK = 1, MODE_DIM = 2 };
enum TrayState : int { TRAY_IDLE = 0, TRAY_BLANK = 1, TRAY_DIM = 2 };

enum : UINT {
    ID_BLANK_ALL = 100,
    ID_BLANK_OTHERS,
    ID_BLANK_CURSOR,
    ID_DIM_ALL,
    ID_WAKE_ALL,
    ID_IDENTIFY,

    ID_WAKE_ANY = 120,
    ID_WAKE_KEYBOARD,
    ID_WAKE_HOTKEY,

    ID_DIM_LEVEL_BASE = 140,  // + index into kDimLevels
    ID_IDLE_BASE      = 160,  // + index into kIdleMinutes

    ID_OPT_FADE = 200,
    ID_OPT_CURSOR,
    ID_OPT_CLOCK,
    ID_OPT_AWAKE,
    ID_OPT_POWEROFF,
    ID_OPT_DDC,
    ID_OPT_LOCK,
    ID_OPT_RESPECT,
    ID_OPT_SWALLOW,
    ID_OPT_INJECTED,

    ID_SCREENS_WINDOW = 290,
    ID_SETTINGS_WINDOW,

    ID_AUTOSTART = 300,
    ID_EDIT_SETTINGS,
    ID_RELOAD,
    ID_ABOUT,
    ID_EXIT,

    ID_MON_BLANK_BASE = 1000, // + monitor index
    ID_MON_DIM_BASE   = 1100, // + monitor index
};

static const int kDimLevels[]  = {20, 35, 50, 65, 80, 90};
static const int kIdleMinutes[] = {0, 1, 2, 5, 10, 15, 30, 60};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

struct Hotkey {
    UINT mods = 0;
    UINT vk = 0;
};

struct Settings {
    Hotkey hkBlank, hkOthers, hkCursor, hkDim;
    int  wakeMode = WAKE_ANY;
    int  mouseThreshold = 30;
    int  graceMs = 1000;
    bool swallowWake = true;
    bool ignoreInjected = true;
    bool fade = true;
    int  fadeMs = 300;
    int  dimPercent = 60;
    int  idleMinutes = 0;
    bool idleRespectBusy = true;
    bool hideCursor = true;
    bool showClock = false;
    int  clockBrightness = 48;
    bool keepAwake = true;
    bool powerOff = false;
    bool ddc = false;
    int  ddcValue = 4;
    bool lockOnWake = false;
};

struct MonitorInfo {
    HMONITOR hmon = nullptr;
    RECT rc{};
    std::wstring device;  // \\.\DISPLAY1 - stable key across re-enumeration
    std::wstring name;    // EDID friendly name
    bool primary = false;
    bool detached = false;  // signal cut by screenBlank (not part of the desktop right now)
    RECT mapRc{};           // where the Screens window draws it (stable while others are detached)
};

struct DetachedInfo {
    std::wstring device, name;
    RECT rc{};
    bool primary = false;
};

struct Overlay {
    std::wstring device;
    HWND hwnd = nullptr;
    int  mode = MODE_BLANK;
    bool wakeOnInput = false;  // part of a "blank all" session
    bool closing = false;
    int  alpha = 0;
    int  target = 255;
    POINT clockPos{-1, -1};
    bool ddcDark = false;  // DDC/CI backlight/power changes applied
    bool sleep = false;    // "Off" rather than plain blank
    int  offMethod = OFF_AUTO;
};

struct IdentifyData {
    int number = 0;
    std::wstring name;
    std::wstring detail;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static HINSTANCE g_inst;
static HWND      g_main;
static Settings  g_s;
static std::wstring g_ini;
static FILETIME  g_iniTime{};
static UINT      WM_TASKBARCREATED;

static std::vector<MonitorInfo> g_monitors;
static std::vector<DetachedInfo> g_detached;          // displays whose signal is cut
static std::vector<std::pair<std::wstring, RECT>> g_layoutSnapshot;  // layout before the first cut
static std::vector<DISPLAYCONFIG_PATH_INFO> g_cfgPaths;  // display config before the first cut
static std::vector<DISPLAYCONFIG_MODE_INFO> g_cfgModes;
static std::vector<Overlay*>    g_overlays;
static std::vector<HWND>        g_identify;

static HHOOK     g_kbHook;
static HHOOK     g_msHook;
static ULONGLONG g_graceUntil;
static ULONGLONG g_lastWake;
static POINT     g_anchor;
static UINT      g_modState;
static bool      g_pendingPowerOff;
static bool      g_monitorsPoweredOff;  // SC_MONITORPOWER sent; needs a nudge to come back

static HWND      g_hScreens;      // Screens window (modeless dialog)
static HWND      g_hSettings;     // Settings window (modeless dialog)
static HWND      g_hMap;          // monitor map control inside the Screens window
static int       g_selMon;        // selected monitor in the map
static std::vector<RECT> g_mapRects;
static WORD      g_hkShown[4];    // hotkey-control values loaded into the Settings window

static void RefreshScreensWindow();

static HICON g_appIcon;
static HICON g_trayIcons[3];
static int   g_trayState = -1;
static bool  g_trayAdded;

static std::wstring g_pendingCmd;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return std::wstring(buf, n);
}

static std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static ULONGLONG Now() { return GetTickCount64(); }

static std::wstring MenuEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'&') out += L'&';
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Log (screenBlank.log next to settings.ini): what was done to which screen and why
// it failed. Kept small: restarted when it passes 512 KB.
// ---------------------------------------------------------------------------

static std::mutex   g_logMx;
static std::wstring g_logPath;

static void Log(const std::wstring& text) {
    if (g_logPath.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMx);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%02d:%02d:%02d.%03d  ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::wstring line = stamp + text + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), n, nullptr, nullptr);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    bool big = GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024;
    HANDLE f = CreateFileW(g_logPath.c_str(), big ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           big ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(f);
}

// ---------------------------------------------------------------------------
// Hotkey parsing ("Ctrl+Alt+B")
// ---------------------------------------------------------------------------

struct KeyName { const wchar_t* name; UINT vk; };
static const KeyName kKeyNames[] = {
    {L"Space", VK_SPACE},     {L"Pause", VK_PAUSE},      {L"ScrollLock", VK_SCROLL},
    {L"Insert", VK_INSERT},   {L"Delete", VK_DELETE},    {L"Home", VK_HOME},
    {L"End", VK_END},         {L"PageUp", VK_PRIOR},     {L"PageDown", VK_NEXT},
    {L"Up", VK_UP},           {L"Down", VK_DOWN},        {L"Left", VK_LEFT},
    {L"Right", VK_RIGHT},     {L"Tab", VK_TAB},          {L"Enter", VK_RETURN},
    {L"Backspace", VK_BACK},  {L"Esc", VK_ESCAPE},       {L"PrintScreen", VK_SNAPSHOT},
    {L"Minus", VK_OEM_MINUS}, {L"Plus", VK_OEM_PLUS},    {L"Comma", VK_OEM_COMMA},
    {L"Period", VK_OEM_PERIOD},
};

static bool ParseHotkey(const std::wstring& text, Hotkey& out) {
    Hotkey h{};
    std::wstring t = Trim(text);
    if (t.empty() || Lower(t) == L"none" || Lower(t) == L"off") {
        out = h;
        return true;
    }
    size_t start = 0;
    while (start <= t.size()) {
        size_t plus = t.find(L'+', start);
        std::wstring part = Trim(t.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        std::wstring lp = Lower(part);
        if (lp == L"ctrl" || lp == L"control") h.mods |= MOD_CONTROL;
        else if (lp == L"alt") h.mods |= MOD_ALT;
        else if (lp == L"shift") h.mods |= MOD_SHIFT;
        else if (lp == L"win" || lp == L"windows") h.mods |= MOD_WIN;
        else if (part.size() == 1 && std::iswalnum(part[0])) h.vk = std::towupper(part[0]);
        else if (lp.size() >= 2 && lp[0] == L'f' && std::iswdigit(lp[1])) {
            int n = _wtoi(lp.c_str() + 1);
            if (n < 1 || n > 24) return false;
            h.vk = VK_F1 + n - 1;
        } else if (lp.size() == 4 && lp.rfind(L"num", 0) == 0 && std::iswdigit(lp[3])) {
            h.vk = VK_NUMPAD0 + (lp[3] - L'0');
        } else {
            bool found = false;
            for (const auto& k : kKeyNames) {
                if (Lower(k.name) == lp) { h.vk = k.vk; found = true; break; }
            }
            if (!found) return false;
        }
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    if (!h.vk) return false;
    out = h;
    return true;
}

static std::wstring FormatHotkey(const Hotkey& h) {
    if (!h.vk) return L"";
    std::wstring s;
    if (h.mods & MOD_CONTROL) s += L"Ctrl+";
    if (h.mods & MOD_ALT) s += L"Alt+";
    if (h.mods & MOD_SHIFT) s += L"Shift+";
    if (h.mods & MOD_WIN) s += L"Win+";
    if ((h.vk >= L'A' && h.vk <= L'Z') || (h.vk >= L'0' && h.vk <= L'9')) s += static_cast<wchar_t>(h.vk);
    else if (h.vk >= VK_F1 && h.vk <= VK_F24) s += L"F" + std::to_wstring(h.vk - VK_F1 + 1);
    else if (h.vk >= VK_NUMPAD0 && h.vk <= VK_NUMPAD9) s += L"Num" + std::to_wstring(h.vk - VK_NUMPAD0);
    else {
        bool found = false;
        for (const auto& k : kKeyNames) {
            if (k.vk == h.vk) { s += k.name; found = true; break; }
        }
        if (!found) s += L"?";
    }
    return s;
}

// ---------------------------------------------------------------------------
// Settings (INI)
// ---------------------------------------------------------------------------

static const char kDefaultIni[] =
    "; screenBlank settings\r\n"
    "; Save this file and screenBlank picks up the changes within a few seconds.\r\n"
    ";\r\n"
    "; Hotkeys: combine Ctrl, Alt, Shift, Win with one key: A-Z, 0-9, F1-F24,\r\n"
    "; Num0-Num9, Space, Pause, ScrollLock, Insert, Delete, Home, End, PageUp,\r\n"
    "; PageDown, Up, Down, Left, Right. Use None to disable a hotkey.\r\n"
    "\r\n"
    "[Hotkeys]\r\n"
    "; Blank every screen (press again to wake)\r\n"
    "BlankAll=Ctrl+Alt+B\r\n"
    "; Blank every screen except the one the mouse is on\r\n"
    "BlankOthers=Ctrl+Alt+O\r\n"
    "; Blank / unblank only the screen the mouse is on\r\n"
    "BlankCursorMonitor=Ctrl+Alt+M\r\n"
    "; Dim every screen (click-through, you can keep working)\r\n"
    "DimAll=Ctrl+Alt+D\r\n"
    "\r\n"
    "[Settings]\r\n"
    "; What wakes screens blanked with BlankAll:\r\n"
    ";   any      - any key, mouse click, or mouse movement\r\n"
    ";   keyboard - keys only (mouse is ignored)\r\n"
    ";   hotkey   - only the BlankAll hotkey; every other key and click is blocked\r\n"
    "WakeOn=any\r\n"
    "; Pixels the mouse must travel before it wakes the screens\r\n"
    "MouseThreshold=30\r\n"
    "; Milliseconds after blanking during which input is ignored\r\n"
    "GraceMs=1000\r\n"
    "; Swallow the key/click that wakes the screens so it doesn't reach the app below\r\n"
    "SwallowWakeInput=1\r\n"
    "; Ignore synthetic input (mouse jigglers, automation tools) when deciding to wake\r\n"
    "IgnoreInjectedInput=1\r\n"
    "Fade=1\r\n"
    "FadeMs=300\r\n"
    "; Dim strength in percent (10-95)\r\n"
    "DimPercent=60\r\n"
    "; Blank all screens after this many idle minutes (0 = off)\r\n"
    "IdleMinutes=0\r\n"
    "; Don't auto-blank while a video plays or a fullscreen app/presentation runs\r\n"
    "IdleRespectBusy=1\r\n"
    "HideCursor=1\r\n"
    "; Faint clock that jumps to a new spot every 30 seconds (burn-in safe)\r\n"
    "ShowClock=0\r\n"
    "; Clock gray level (10-255)\r\n"
    "ClockBrightness=48\r\n"
    "; Keep the PC from sleeping while screens are blanked\r\n"
    "KeepAwake=1\r\n"
    "; After blanking all screens, also ask Windows to power the monitors down\r\n"
    "PowerOff=0\r\n"
    "; Experimental: put each blanked monitor into standby over DDC/CI\r\n"
    "DdcStandby=0\r\n"
    "; Preferred DDC/CI power mode: 2 standby, 3 suspend, 4 off. Mode 5 (hard off) is never\r\n"
    "; used: many monitors only come back from it with their power button.\r\n"
    "DdcPowerValue=4\r\n"
    "; Lock Windows when screens wake from a BlankAll session\r\n"
    "LockOnWake=0\r\n"
    "\r\n"
    "; How \"Off\" turns each screen off, by screen name.\r\n"
    "; Set from the Screens window. 0 auto (standby if supported, else black),\r\n"
    "; 2 cut the signal (windows put back), 3 black only (monitor never touched).\r\n"
    "[OffMethod]\r\n";

static void InitIniPath() {
    std::wstring portable = ExeDir() + L"\\settings.ini";
    if (FileExists(portable)) {
        g_ini = portable;
        return;
    }
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        std::wstring dir = std::wstring(appdata) + L"\\screenBlank";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_ini = dir + L"\\settings.ini";
    } else {
        g_ini = portable;
    }
}

static void RememberIniTime() {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(g_ini.c_str(), GetFileExInfoStandard, &fa)) g_iniTime = fa.ftLastWriteTime;
}

static void EnsureIniExists() {
    if (FileExists(g_ini)) return;
    HANDLE f = CreateFileW(g_ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, kDefaultIni, static_cast<DWORD>(sizeof(kDefaultIni) - 1), &written, nullptr);
    CloseHandle(f);
}

static std::wstring IniStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[256];
    GetPrivateProfileStringW(section, key, def, buf, ARRAYSIZE(buf), g_ini.c_str());
    return Trim(buf);
}

static int IniInt(const wchar_t* key, int def, int lo, int hi) {
    int v = static_cast<int>(GetPrivateProfileIntW(L"Settings", key, def, g_ini.c_str()));
    return std::clamp(v, lo, hi);
}

static bool IniBool(const wchar_t* key, bool def) {
    return GetPrivateProfileIntW(L"Settings", key, def ? 1 : 0, g_ini.c_str()) != 0;
}

static void IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_ini.c_str());
}

static void IniWriteInt(const wchar_t* key, int v) { IniWrite(L"Settings", key, std::to_wstring(v)); }
static void IniWriteBool(const wchar_t* key, bool v) { IniWrite(L"Settings", key, v ? L"1" : L"0"); }

static Hotkey LoadHotkey(const wchar_t* key, const wchar_t* def) {
    Hotkey h;
    if (!ParseHotkey(IniStr(L"Hotkeys", key, def), h)) ParseHotkey(def, h);
    return h;
}

static const wchar_t* WakeModeName(int m) {
    return m == WAKE_KEYBOARD ? L"keyboard" : m == WAKE_HOTKEY ? L"hotkey" : L"any";
}

static void LoadSettings() {
    EnsureIniExists();
    Settings s;
    s.hkBlank  = LoadHotkey(L"BlankAll", L"Ctrl+Alt+B");
    s.hkOthers = LoadHotkey(L"BlankOthers", L"Ctrl+Alt+O");
    s.hkCursor = LoadHotkey(L"BlankCursorMonitor", L"Ctrl+Alt+M");
    s.hkDim    = LoadHotkey(L"DimAll", L"Ctrl+Alt+D");

    std::wstring wake = Lower(IniStr(L"Settings", L"WakeOn", L"any"));
    s.wakeMode = wake == L"keyboard" ? WAKE_KEYBOARD : wake == L"hotkey" ? WAKE_HOTKEY : WAKE_ANY;
    // Hotkey-only wake with no hotkey would leave no way back but Ctrl+Alt+Del.
    if (s.wakeMode == WAKE_HOTKEY && !s.hkBlank.vk) s.wakeMode = WAKE_KEYBOARD;

    s.mouseThreshold  = IniInt(L"MouseThreshold", 30, 1, 2000);
    s.graceMs         = IniInt(L"GraceMs", 1000, 0, 10000);
    s.swallowWake     = IniBool(L"SwallowWakeInput", true);
    s.ignoreInjected  = IniBool(L"IgnoreInjectedInput", true);
    s.fade            = IniBool(L"Fade", true);
    s.fadeMs          = IniInt(L"FadeMs", 300, 50, 5000);
    s.dimPercent      = IniInt(L"DimPercent", 60, 10, 95);
    s.idleMinutes     = IniInt(L"IdleMinutes", 0, 0, 1440);
    s.idleRespectBusy = IniBool(L"IdleRespectBusy", true);
    s.hideCursor      = IniBool(L"HideCursor", true);
    s.showClock       = IniBool(L"ShowClock", false);
    s.clockBrightness = IniInt(L"ClockBrightness", 48, 10, 255);
    s.keepAwake       = IniBool(L"KeepAwake", true);
    s.powerOff        = IniBool(L"PowerOff", false);
    s.ddc             = IniBool(L"DdcStandby", false);
    s.ddcValue        = IniInt(L"DdcPowerValue", 4, 2, 4);
    s.lockOnWake      = IniBool(L"LockOnWake", false);
    g_s = s;
    RememberIniTime();
}

static void SaveSettings() {
    auto hk = [](const Hotkey& h) { std::wstring s = FormatHotkey(h); return s.empty() ? std::wstring(L"None") : s; };
    IniWrite(L"Hotkeys", L"BlankAll", hk(g_s.hkBlank));
    IniWrite(L"Hotkeys", L"BlankOthers", hk(g_s.hkOthers));
    IniWrite(L"Hotkeys", L"BlankCursorMonitor", hk(g_s.hkCursor));
    IniWrite(L"Hotkeys", L"DimAll", hk(g_s.hkDim));
    IniWrite(L"Settings", L"WakeOn", WakeModeName(g_s.wakeMode));
    IniWriteInt(L"MouseThreshold", g_s.mouseThreshold);
    IniWriteBool(L"SwallowWakeInput", g_s.swallowWake);
    IniWriteBool(L"IgnoreInjectedInput", g_s.ignoreInjected);
    IniWriteBool(L"Fade", g_s.fade);
    IniWriteInt(L"DimPercent", g_s.dimPercent);
    IniWriteInt(L"IdleMinutes", g_s.idleMinutes);
    IniWriteBool(L"IdleRespectBusy", g_s.idleRespectBusy);
    IniWriteBool(L"HideCursor", g_s.hideCursor);
    IniWriteBool(L"ShowClock", g_s.showClock);
    IniWriteBool(L"KeepAwake", g_s.keepAwake);
    IniWriteBool(L"PowerOff", g_s.powerOff);
    IniWriteBool(L"DdcStandby", g_s.ddc);
    IniWriteBool(L"LockOnWake", g_s.lockOnWake);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ini.c_str()); // flush cache
    RememberIniTime();
}

// ---------------------------------------------------------------------------
// Autostart (HKCU Run)
// ---------------------------------------------------------------------------

static bool AutostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, kAppName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

static void SetAutostart(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        std::wstring cmd = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kAppName);
    }
    RegCloseKey(k);
}

// ---------------------------------------------------------------------------
// Explorer right-click menu (desktop and folder background)
// ---------------------------------------------------------------------------
//
// A classic cascading menu under HKCU, no admin rights. On Windows 11 it shows under
// "Show more options" (or Shift+right-click). "This screen" is the screen the mouse
// is on, i.e. the one that was right-clicked.

static const wchar_t kMenuKey[] = L"Software\\Classes\\Directory\\Background\\shell\\screenBlank";

struct MenuEntry { const wchar_t* key; const wchar_t* text; const wchar_t* args; bool separatorAfter; };
static const MenuEntry kMenuEntries[] = {
    {L"01cursor", L"Blank this screen", L"cursor", false},
    {L"02off", L"Turn this screen off", L"off-cursor", false},
    {L"03others", L"Blank all except this screen", L"others", false},
    {L"04blank", L"Blank all screens", L"blank", false},
    {L"05dim", L"Dim / undim all screens", L"dim", true},
    {L"06on", L"Turn all screens on", L"on-all", true},
    {L"07screens", L"Screens...", L"screens", false},
    {L"08settings", L"Settings...", L"settings", false},
};

static bool ExplorerMenuInstalled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kMenuKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}

static void SetRegString(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                   static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
}

static void SetExplorerMenu(bool on) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kMenuKey);
    if (!on) return;
    const std::wstring exe = ExePath();
    HKEY root;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kMenuKey, 0, nullptr, 0, KEY_WRITE, nullptr, &root, nullptr) != ERROR_SUCCESS)
        return;
    SetRegString(root, L"MUIVerb", kAppName);
    SetRegString(root, L"Icon", L"\"" + exe + L"\",0");
    SetRegString(root, L"SubCommands", L"");  // empty = cascade from the "shell" subkey
    RegCloseKey(root);
    for (const auto& e : kMenuEntries) {
        std::wstring path = std::wstring(kMenuKey) + L"\\shell\\" + e.key;
        HKEY item;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &item, nullptr) !=
            ERROR_SUCCESS)
            continue;
        SetRegString(item, L"MUIVerb", e.text);
        if (e.separatorAfter) {
            DWORD flags = 0x40;  // ECF_SEPARATORAFTER
            RegSetValueExW(item, L"CommandFlags", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&flags), sizeof(flags));
        }
        HKEY cmd;
        if (RegCreateKeyExW(item, L"command", 0, nullptr, 0, KEY_WRITE, nullptr, &cmd, nullptr) == ERROR_SUCCESS) {
            SetRegString(cmd, nullptr, L"\"" + exe + L"\" " + e.args);
            RegCloseKey(cmd);
        }
        RegCloseKey(item);
    }
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------

// Maps GDI device names (\\.\DISPLAY1) to EDID friendly names ("DELL U2720Q").
static std::vector<std::pair<std::wstring, std::wstring>> FriendlyNames() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS)
        return out;
    for (UINT32 i = 0; i < np; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = paths[i].targetInfo.adapterId;
        tgt.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;

        std::wstring name = tgt.monitorFriendlyDeviceName;
        if (name.empty()) {
            auto tech = tgt.outputTechnology;
            if (tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED)
                name = L"Built-in display";
        }
        out.emplace_back(src.viewGdiDeviceName, name);
    }
    return out;
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(h, &mi)) {
        MonitorInfo m;
        m.hmon = h;
        m.rc = mi.rcMonitor;
        m.device = mi.szDevice;
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        list->push_back(m);
    }
    return TRUE;
}

static void RefreshMonitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&list));
    auto names = FriendlyNames();
    for (auto& m : list) {
        for (const auto& n : names) {
            if (_wcsicmp(n.first.c_str(), m.device.c_str()) == 0) { m.name = n.second; break; }
        }
        m.mapRc = m.rc;
    }
    // Displays with their signal cut aren't enumerated; keep showing them.
    for (const auto& d : g_detached) {
        bool present = false;
        for (const auto& m : list)
            if (_wcsicmp(m.device.c_str(), d.device.c_str()) == 0) present = true;
        if (present) continue;
        MonitorInfo m;
        m.device = d.device;
        m.name = d.name;
        m.rc = d.rc;
        m.primary = d.primary;
        m.detached = true;
        list.push_back(m);
    }
    // While something is cut, Windows reflows the rest; draw everything at its old place.
    if (!g_detached.empty())
        for (auto& m : list)
            for (const auto& s : g_layoutSnapshot)
                if (_wcsicmp(s.first.c_str(), m.device.c_str()) == 0) m.mapRc = s.second;
    std::sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.mapRc.left != b.mapRc.left) return a.mapRc.left < b.mapRc.left;
        return a.mapRc.top < b.mapRc.top;
    });
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name.empty()) list[i].name = L"Display " + std::to_wstring(i + 1);
    if (!g_detached.empty()) {
        std::wstring s = L"map while cut:";
        for (const auto& m : list)
            s += L" [" + m.device + L" " + std::to_wstring(m.mapRc.left) + L"," + std::to_wstring(m.mapRc.top) +
                 (m.detached ? L" cut]" : L"]");
        s += L" snapshot:";
        for (const auto& p : g_layoutSnapshot) s += L" " + p.first + L"@" + std::to_wstring(p.second.left);
        Log(s);
    }
    g_monitors = std::move(list);
}

static const MonitorInfo* FindMonitor(const std::wstring& device) {
    for (const auto& m : g_monitors)
        if (_wcsicmp(m.device.c_str(), device.c_str()) == 0) return &m;
    return nullptr;
}

static int CursorMonitorIndex() {
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR h = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    for (size_t i = 0; i < g_monitors.size(); ++i)
        if (!g_monitors[i].detached && g_monitors[i].hmon == h) return static_cast<int>(i);
    return g_monitors.empty() ? -1 : 0;
}

static std::wstring MonitorLabel(size_t i) {
    const auto& m = g_monitors[i];
    std::wstring s = std::to_wstring(i + 1) + L"   " + MenuEscape(m.name) + L"   (" +
                     std::to_wstring(m.rc.right - m.rc.left) + L"x" + std::to_wstring(m.rc.bottom - m.rc.top);
    if (m.primary) s += L", primary";
    return s + L")";
}

// ---------------------------------------------------------------------------
// DDC/CI: backlight and power control on a worker thread
// ---------------------------------------------------------------------------
//
// An LCD keeps its backlight lit behind a black window, so "black" still glows.
// Over DDC/CI most monitors accept brightness and contrast 0, and many accept a
// power mode (VCP D6). Original values are written to ddc-restore.ini before the
// monitor is touched, so a crash or power cut still gets them restored on the
// next start. DDC/CI calls take 40-100 ms each, hence the worker thread.

struct DdcOptions {
    bool backlight = false;
    bool power = false;
    int  powerValue = 4;
    bool hard = false;  // DDC/CI mode 5: full power off, power light off too
};

struct DdcJob {
    std::wstring device;
    bool dark = false;
    DdcOptions opt;
};

struct DdcDevice {
    std::vector<PHYSICAL_MONITOR> handles;  // kept open while the monitor is dark
    bool capsRead = false;
    bool hasCaps = false;
    std::vector<int> powerModes;  // D6 values listed in the capability string
    std::vector<int> inputs;      // 60 (input source) values listed in the capability string
};

static std::mutex              g_ddcMx;
static std::condition_variable g_ddcCv;
static std::deque<DdcJob>      g_ddcQueue;
static bool                    g_ddcQuit;
static std::thread             g_ddcThread;
static std::wstring            g_ddcStateFile;

static const wchar_t kDdcSection[] = L"Saved";

// --- worker-thread side ---

struct ResolveCtx { const std::wstring* device; HMONITOR found; };

static BOOL CALLBACK ResolveMonitorProc(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    auto* c = reinterpret_cast<ResolveCtx*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(h, &mi) && _wcsicmp(mi.szDevice, c->device->c_str()) == 0) {
        c->found = h;
        return FALSE;
    }
    return TRUE;
}

static bool OpenPhysical(const std::wstring& device, std::vector<PHYSICAL_MONITOR>& out) {
    ResolveCtx ctx{&device, nullptr};
    EnumDisplayMonitors(nullptr, nullptr, ResolveMonitorProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found) return false;
    DWORD n = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(ctx.found, &n) || n == 0) return false;
    out.resize(n);
    if (!GetPhysicalMonitorsFromHMONITOR(ctx.found, n, out.data())) {
        out.clear();
        return false;
    }
    return true;
}

static void ClosePhysical(std::vector<PHYSICAL_MONITOR>& v) {
    if (!v.empty()) DestroyPhysicalMonitors(static_cast<DWORD>(v.size()), v.data());
    v.clear();
}

// Values listed for one VCP code in a capability string, e.g. code "d6" in "... D6(01 05) ...".
static std::vector<int> CapsValues(const std::string& caps, size_t vcpStart, const char* code) {
    std::vector<int> out;
    std::string key = std::string(code) + "(";
    size_t p = vcpStart;
    while ((p = caps.find(key, p + 1)) != std::string::npos) {
        char before = caps[p - 1];
        if (before == ' ' || before == '(') break;
    }
    if (p == std::string::npos) return out;
    const char* s = caps.c_str() + p + key.size();
    while (*s && *s != ')') {
        char* end = nullptr;
        long v = std::strtol(s, &end, 16);
        if (end == s) { ++s; continue; }
        out.push_back(static_cast<int>(v));
        s = end;
    }
    return out;
}

static void ReadCaps(HANDLE h, DdcDevice& d) {
    d.capsRead = true;
    DWORD len = 0;
    if (!GetCapabilitiesStringLength(h, &len) || len == 0) return;
    std::string caps(len, '\0');
    if (!CapabilitiesRequestAndCapabilitiesReply(h, caps.data(), len)) return;
    for (auto& ch : caps) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    size_t vcp = caps.find("vcp(");
    if (vcp == std::string::npos) return;
    d.hasCaps = true;
    d.powerModes = CapsValues(caps, vcp, "d6");
    d.inputs = CapsValues(caps, vcp, "60");
}

static int PickPowerValue(const DdcDevice& d, const DdcOptions& o) {
    auto has = [&](int v) { return std::find(d.powerModes.begin(), d.powerModes.end(), v) != d.powerModes.end(); };
    // Power off was chosen explicitly for this screen in the Screens window.
    if (o.hard) return (!d.hasCaps || has(5)) ? 5 : 0;
    if (!d.hasCaps) return o.powerValue;  // no capability string: try what was asked for
    // Auto never uses mode 5 (power off): many monitors can't be woken from it except by their button.
    if (has(o.powerValue) && o.powerValue != 5) return o.powerValue;
    for (int v : {4, 2, 3})
        if (has(v)) return v;
    return 0;
}

// Saved entry: brightness, contrast, power-mode-sent flag, original input (-1 = untouched),
// then any extra picture settings as code:value pairs.
struct DdcSaved {
    int b = -1, c = -1, p = 0, input = -1;
    std::vector<std::pair<int, int>> extra;
};

static bool ReadSaved(const std::wstring& device, DdcSaved& s) {
    wchar_t buf[256];
    GetPrivateProfileStringW(kDdcSection, device.c_str(), L"", buf, ARRAYSIZE(buf), g_ddcStateFile.c_str());
    if (!buf[0]) return false;
    s = DdcSaved{};
    swscanf_s(buf, L"%d,%d,%d,%d", &s.b, &s.c, &s.p, &s.input);
    const wchar_t* rest = buf;
    for (int commas = 0; *rest && commas < 4; ++rest)
        if (*rest == L',') ++commas;
    while (*rest) {
        unsigned code = 0;
        int value = 0;
        if (swscanf_s(rest, L"%x:%d", &code, &value) == 2) s.extra.emplace_back(static_cast<int>(code), value);
        const wchar_t* next = wcschr(rest, L';');
        if (!next) break;
        rest = next + 1;
    }
    return true;
}

static void WriteSaved(const std::wstring& device, const DdcSaved& s) {
    std::wstring v = std::to_wstring(s.b) + L"," + std::to_wstring(s.c) + L"," + std::to_wstring(s.p) + L"," +
                     std::to_wstring(s.input);
    if (!s.extra.empty()) {
        v += L",";
        for (size_t i = 0; i < s.extra.size(); ++i) {
            wchar_t pair[32];
            swprintf_s(pair, L"%s%02X:%d", i ? L";" : L"", s.extra[i].first, s.extra[i].second);
            v += pair;
        }
    }
    WritePrivateProfileStringW(kDdcSection, device.c_str(), v.c_str(), g_ddcStateFile.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ddcStateFile.c_str());
}

// Only ever a power-mode change (standby), which monitors don't save. screenBlank never
// changes picture settings or the input: monitors save those to a settings memory that
// wears a little with every write.
static void DdcDark(const DdcJob& j, DdcDevice& d) {
    if (!j.opt.power) return;
    if (d.handles.empty() && !OpenPhysical(j.device, d.handles)) { Log(L"standby: can't reach " + j.device); return; }
    HANDLE h0 = d.handles[0].hPhysicalMonitor;
    if (!d.capsRead) ReadCaps(h0, d);
    int pv = PickPowerValue(d, j.opt);
    if (!pv) {
        Log(L"standby: " + j.device + L" has no standby mode; it stays black only");
        return;
    }
    DdcSaved s;
    ReadSaved(j.device, s);
    s.p = 1;
    WriteSaved(j.device, s);  // record first, so a crash still gets it woken
    for (auto& pm : d.handles) SetVCPFeature(pm.hPhysicalMonitor, 0xD6, static_cast<DWORD>(pv));
    Log(L"standby: " + j.device + L" mode " + std::to_wstring(pv));
}
static void DdcRestore(const std::wstring& device, DdcDevice& d, bool forcePowerOn) {
    DdcSaved s;
    if (!ReadSaved(device, s)) {
        // "On" for a screen screenBlank didn't touch: wake it from standby anyway
        // (e.g. Windows put it to sleep), so nobody has to reach for its power button.
        if (forcePowerOn && (d.handles.empty() ? OpenPhysical(device, d.handles) : true))
            for (auto& pm : d.handles) SetVCPFeature(pm.hPhysicalMonitor, 0xD6, 1);
        ClosePhysical(d.handles);
        return;
    }
    // A monitor coming out of standby can take a moment to answer again.
    for (int attempt = 0; d.handles.empty() && attempt < 5; ++attempt) {
        if (OpenPhysical(device, d.handles)) break;
        Sleep(1000);
    }
    if (d.handles.empty()) return;  // keep the saved entry; retried on next start
    // A monitor waking from standby (or relocking onto its input) acknowledges commands it
    // then ignores, so every value is read back and the restore repeats until it sticks.
    auto setAndCheck = [](HANDLE h, BYTE code, int value) {
        if (value < 0) return true;
        SetVCPFeature(h, code, static_cast<DWORD>(value));
        // An input change keeps the monitor busy (no DDC/CI replies) for a moment.
        for (int wait = 0; wait < (code == 0x60 ? 15 : 1); ++wait) {
            Sleep(code == 0x60 ? 200 : 150);
            DWORD cur = 0, max = 0;
            if (GetVCPFeatureAndVCPFeatureReply(h, code, nullptr, &cur, &max) &&
                static_cast<int>(code == 0x60 ? (cur & 0xFF) : cur) == value)
                return true;
        }
        return false;
    };
    bool ok = false;
    for (int attempt = 0; attempt < 10 && !ok; ++attempt) {
        if (attempt) Sleep(1000);
        ok = true;
        for (auto& pm : d.handles) {
            HANDLE h = pm.hPhysicalMonitor;
            if (s.p && !setAndCheck(h, 0xD6, 1)) ok = false;
            if (s.input >= 0 && !setAndCheck(h, 0x60, s.input)) ok = false;
            if ((s.p || s.input >= 0) && attempt == 0) Sleep(1500);  // let it finish waking
            if (!setAndCheck(h, 0x10, s.b)) ok = false;
            if (!setAndCheck(h, 0x12, s.c)) ok = false;
            for (const auto& e : s.extra)
                if (!setAndCheck(h, static_cast<BYTE>(e.first), e.second)) ok = false;
        }
    }
    Log(L"ddc restore " + device + (ok ? L" ok" : L" NOT confirmed, kept for retry"));
    if (ok) {
        WritePrivateProfileStringW(kDdcSection, device.c_str(), nullptr, g_ddcStateFile.c_str());
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ddcStateFile.c_str());
    }
    ClosePhysical(d.handles);
}

static void DdcWorker() {
    std::vector<std::pair<std::wstring, DdcDevice>> devices;
    auto deviceFor = [&](const std::wstring& name) -> DdcDevice& {
        for (auto& e : devices)
            if (_wcsicmp(e.first.c_str(), name.c_str()) == 0) return e.second;
        devices.emplace_back(name, DdcDevice{});
        return devices.back().second;
    };
    for (;;) {
        DdcJob job;
        {
            std::unique_lock<std::mutex> lock(g_ddcMx);
            g_ddcCv.wait(lock, [] { return g_ddcQuit || !g_ddcQueue.empty(); });
            if (g_ddcQueue.empty()) break;  // quit requested and nothing left to restore
            job = std::move(g_ddcQueue.front());
            g_ddcQueue.pop_front();
        }
        DdcDevice& d = deviceFor(job.device);
        if (job.dark) DdcDark(job, d);
        else DdcRestore(job.device, d, job.opt.power);
    }
    for (auto& e : devices) ClosePhysical(e.second.handles);
}

// --- UI-thread side ---

static void DdcQueue(const std::wstring& device, bool dark, const DdcOptions& opt = DdcOptions{}) {
    DdcJob j;
    j.device = device;
    j.dark = dark;
    j.opt = opt;
    {
        std::lock_guard<std::mutex> lock(g_ddcMx);
        g_ddcQueue.push_back(std::move(j));
    }
    g_ddcCv.notify_one();
}

static void DdcStart() {
    size_t slash = g_ini.find_last_of(L"\\/");
    g_ddcStateFile = (slash == std::wstring::npos ? std::wstring(L".") : g_ini.substr(0, slash)) + L"\\ddc-restore.ini";
    g_ddcThread = std::thread(DdcWorker);
    // Monitors left dark by a crash or power cut get their settings back now.
    std::vector<wchar_t> keys(4096);
    DWORD n = GetPrivateProfileStringW(kDdcSection, nullptr, L"", keys.data(), static_cast<DWORD>(keys.size()),
                                       g_ddcStateFile.c_str());
    for (const wchar_t* k = keys.data(); n && *k; k += wcslen(k) + 1) DdcQueue(k, false);
}

static void DdcStop() {
    {
        std::lock_guard<std::mutex> lock(g_ddcMx);
        g_ddcQuit = true;
    }
    g_ddcCv.notify_one();
    if (g_ddcThread.joinable()) g_ddcThread.join();
}

// What DDC/CI does to a blanked monitor. "Off" (o->sleep) follows the screen's off method;
// a plain blank follows the global options.
static DdcOptions DdcOptionsFor(const Overlay* o) {
    DdcOptions opt;
    opt.powerValue = g_s.ddcValue;
    // Standby is the only thing ever sent: Off with Auto, or plain Blank with the standby option.
    opt.power = o->sleep ? (o->offMethod == OFF_AUTO || o->offMethod == OFF_HARD) : g_s.ddc;
    opt.hard = o->sleep && o->offMethod == OFF_HARD;
    return opt;
}

static void OverlayDdcDark(Overlay* o) {
    if (o->ddcDark) return;
    DdcOptions opt = DdcOptionsFor(o);
    if (!opt.power) return;
    o->ddcDark = true;
    DdcQueue(o->device, true, opt);
}

// ---------------------------------------------------------------------------
// Cutting the signal: the off switch for monitors that can't sleep
// ---------------------------------------------------------------------------
//
// Some monitors (e.g. Lenovo LT2452p) ignore Windows' monitor-off and only know a
// DDC/CI hard power-off they can't wake from. Every monitor does go to standby when
// it loses its signal, so screenBlank can take the display off the desktop
// (SetDisplayConfig, not saved to Windows' display database) and put it back on
// wake. Windows rearranges windows when a display goes away, so every window's
// placement is saved first and put back after the display returns.

static const wchar_t kOffSection[] = L"OffMethod";

static int OffMethodFor(const std::wstring& name) {
    int v = static_cast<int>(GetPrivateProfileIntW(kOffSection, name.c_str(), OFF_AUTO, g_ini.c_str()));
    // 1 was the removed empty-input method; it falls back to Auto.
    return (v == OFF_CUT || v == OFF_BLACK || v == OFF_HARD) ? v : OFF_AUTO;
}

static void SetOffMethodFor(const std::wstring& name, int method) {
    std::wstring v = std::to_wstring(method);
    WritePrivateProfileStringW(kOffSection, name.c_str(), method == OFF_AUTO ? nullptr : v.c_str(), g_ini.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ini.c_str());
    RememberIniTime();
}

struct SavedWindow {
    HWND hwnd;
    WINDOWPLACEMENT wp;
};
static std::vector<SavedWindow> g_savedWindows;

static BOOL CALLBACK SnapshotWindowProc(HWND h, LPARAM) {
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    wchar_t cls[64];
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return TRUE;
    SavedWindow s{h, {}};
    s.wp.length = sizeof(s.wp);
    if (GetWindowPlacement(h, &s.wp)) g_savedWindows.push_back(s);
    return TRUE;
}

static void SnapshotWindows() {
    g_savedWindows.clear();
    EnumWindows(SnapshotWindowProc, 0);
}

// Run twice after the display returns: Windows itself moves things around in between.
static void RestoreWindows() {
    for (const auto& s : g_savedWindows) {
        if (!IsWindow(s.hwnd)) continue;
        WINDOWPLACEMENT wp = s.wp;
        wp.flags |= WPF_ASYNCWINDOWPLACEMENT;  // never hang on an unresponsive app
        if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE) {
            wp.showCmd = SW_SHOWMINNOACTIVE;
        } else if (wp.showCmd == SW_SHOWMAXIMIZED) {
            // A maximized window stays on the monitor it's maximized on; un-maximize it onto
            // its old monitor first, then maximize it there.
            WINDOWPLACEMENT normal = wp;
            normal.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(s.hwnd, &normal);
        } else {
            wp.showCmd = SW_SHOWNOACTIVATE;
        }
        SetWindowPlacement(s.hwnd, &wp);
    }
}

static void MarkDetachedOnDisk(bool on) {
    WritePrivateProfileStringW(L"Display", L"SignalCut", on ? L"1" : nullptr, g_ddcStateFile.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ddcStateFile.c_str());
}

static bool QueryActiveConfig(std::vector<DISPLAYCONFIG_PATH_INFO>& paths, std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
        paths.resize(np);
        modes.resize(nm);
        LONG r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr);
        if (r == ERROR_INSUFFICIENT_BUFFER) continue;  // layout changed between calls
        if (r != ERROR_SUCCESS) return false;
        paths.resize(np);
        modes.resize(nm);
        return true;
    }
    return false;
}

static std::wstring PathSourceName(const DISPLAYCONFIG_PATH_INFO& p) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
    src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    src.header.size = sizeof(src);
    src.header.adapterId = p.sourceInfo.adapterId;
    src.header.id = p.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) return L"";
    return src.viewGdiDeviceName;
}

// Cuts the signal to the given monitors. Refuses to cut every display.
static bool DetachMonitors(const std::vector<size_t>& indices) {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!QueryActiveConfig(paths, modes)) { Log(L"cut signal: QueryDisplayConfig failed"); return false; }
    // Copies, not pointers: SetDisplayConfig sends WM_DISPLAYCHANGE synchronously, and any
    // handler that refreshes g_monitors during it would leave pointers dangling.
    std::vector<MonitorInfo> targetCopies;
    for (size_t i : indices)
        if (i < g_monitors.size() && !g_monitors[i].detached) targetCopies.push_back(g_monitors[i]);
    std::vector<const MonitorInfo*> targets;
    for (const auto& t : targetCopies) targets.push_back(&t);
    if (targets.empty()) { Log(L"cut signal: no matching screen (already cut?)"); return false; }

    // Leave the cut displays' paths out entirely and copy over only the modes still in use;
    // SetDisplayConfig rejects inactive paths or orphaned modes in a supplied config.
    std::vector<DISPLAYCONFIG_PATH_INFO> keep;
    std::vector<DISPLAYCONFIG_MODE_INFO> keepModes;
    size_t cut = 0;
    for (const auto& p : paths) {
        std::wstring name = PathSourceName(p);
        bool drop = false;
        for (const auto* t : targets)
            if (_wcsicmp(name.c_str(), t->device.c_str()) == 0) drop = true;
        if (drop) {
            ++cut;
            continue;
        }
        DISPLAYCONFIG_PATH_INFO q = p;
        if (q.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && q.sourceInfo.modeInfoIdx < modes.size()) {
            keepModes.push_back(modes[q.sourceInfo.modeInfoIdx]);
            q.sourceInfo.modeInfoIdx = static_cast<UINT32>(keepModes.size() - 1);
        }
        if (q.targetInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && q.targetInfo.modeInfoIdx < modes.size()) {
            keepModes.push_back(modes[q.targetInfo.modeInfoIdx]);
            q.targetInfo.modeInfoIdx = static_cast<UINT32>(keepModes.size() - 1);
        }
        keep.push_back(q);
    }
    if (cut == 0 || keep.empty()) {
        Log(L"cut signal: refused, paths=" + std::to_wstring(paths.size()) + L" cut=" + std::to_wstring(cut) +
            L" (a screen must stay connected, or the display wasn't found)");
        return false;
    }

    // One display must sit at 0,0 (the primary). If the primary is being cut, the remaining
    // display nearest the origin becomes primary and everything shifts with it.
    bool hasOrigin = false;
    POINTL shift{0, 0};
    long best = LONG_MAX;
    for (const auto& md : keepModes) {
        if (md.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) continue;
        const POINTL& pos = md.sourceMode.position;
        if (pos.x == 0 && pos.y == 0) hasOrigin = true;
        long d = std::labs(pos.x) + std::labs(pos.y);
        if (d < best) { best = d; shift = pos; }
    }
    if (!hasOrigin)
        for (auto& md : keepModes)
            if (md.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                md.sourceMode.position.x -= shift.x;
                md.sourceMode.position.y -= shift.y;
            }
    paths = std::move(keep);
    modes = std::move(keepModes);

    if (g_detached.empty()) {
        SnapshotWindows();
        std::vector<DISPLAYCONFIG_PATH_INFO> snapPaths;
        std::vector<DISPLAYCONFIG_MODE_INFO> snapModes;
        if (!QueryActiveConfig(snapPaths, snapModes)) { Log(L"cut signal: snapshot query failed"); return false; }
        g_cfgPaths = std::move(snapPaths);
        g_cfgModes = std::move(snapModes);
        g_layoutSnapshot.clear();
        for (const auto& m : g_monitors) g_layoutSnapshot.emplace_back(m.device, m.mapRc);
    }
    MarkDetachedOnDisk(true);  // before touching anything, so a crash still gets it undone
    LONG r = SetDisplayConfig(static_cast<UINT32>(paths.size()), paths.data(), static_cast<UINT32>(modes.size()),
                              modes.data(), SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
    Log(L"cut signal: SetDisplayConfig result " + std::to_wstring(r) + L" (0 = ok), kept " + std::to_wstring(paths.size()) +
        L" path(s), primary moved: " + (hasOrigin ? L"no" : L"yes"));
    if (r != ERROR_SUCCESS) {
        if (g_detached.empty()) MarkDetachedOnDisk(false);
        return false;
    }
    for (const auto* t : targets) g_detached.push_back({t->device, t->name, t->rc, t->primary});
    return true;
}

// Puts every cut display back the way it was.
static void ReattachMonitors() {
    if (g_detached.empty()) return;
    LONG r = SetDisplayConfig(static_cast<UINT32>(g_cfgPaths.size()), g_cfgPaths.data(),
                              static_cast<UINT32>(g_cfgModes.size()), g_cfgModes.data(),
                              SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
    if (r != ERROR_SUCCESS) SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_USE_DATABASE_CURRENT);
    g_detached.clear();
    MarkDetachedOnDisk(false);
    SetTimer(g_main, TIMER_RESTORE_WINDOWS_1, 1500, nullptr);
    SetTimer(g_main, TIMER_RESTORE_WINDOWS_2, 4000, nullptr);
}

// After a crash with a display cut, Windows' saved layout brings it back.
static void RecoverDetachedOnStart() {
    if (GetPrivateProfileIntW(L"Display", L"SignalCut", 0, g_ddcStateFile.c_str()) == 0) return;
    SetDisplayConfig(0, nullptr, 0, nullptr, SDC_APPLY | SDC_USE_DATABASE_CURRENT);
    MarkDetachedOnDisk(false);
}

static void OverlayDdcRestore(Overlay* o) {
    if (!o->ddcDark) return;
    o->ddcDark = false;
    DdcQueue(o->device, false);
}

// ---------------------------------------------------------------------------
// Tray icon (drawn at runtime so the three states stay in sync with the exe icon)
// ---------------------------------------------------------------------------

static HICON MakeTrayIcon(int state) {
    int s = std::max(16, GetSystemMetrics(SM_CXSMICON));
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s;
    bi.bmiHeader.biHeight = -s;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;

    const double u = s / 16.0;
    auto in = [&](int x, int y, double x0, double y0, double x1, double y1) {
        double cx = x + 0.5, cy = y + 0.5;
        return cx >= x0 * u && cx < x1 * u && cy >= y0 * u && cy < y1 * u;
    };
    const uint32_t frame  = 0xFFC4C9D0;
    const uint32_t screen = state == TRAY_BLANK ? 0xFF000000 : state == TRAY_DIM ? 0xFF505868 : 0xFF2F80ED;
    const uint32_t mark   = 0xFFC4C9D0;
    auto* px = static_cast<uint32_t*>(bits);
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            uint32_t c = 0;
            if (in(x, y, 0.5, 2.0, 15.5, 12.0)) c = frame;
            if (in(x, y, 1.75, 3.25, 14.25, 10.75)) c = screen;
            if (in(x, y, 7.0, 12.0, 9.0, 13.5)) c = frame;
            if (in(x, y, 4.5, 13.5, 11.5, 15.0)) c = frame;
            // Blanked: tiny "off" bar in the middle of the black screen.
            if (state == TRAY_BLANK && in(x, y, 6.0, 6.5, 10.0, 7.5)) c = mark;
            px[y * s + x] = c;
        }
    }
    std::vector<BYTE> maskBits(static_cast<size_t>((s + 15) / 16) * 2 * s, 0);
    HBITMAP mask = CreateBitmap(s, s, 1, 1, maskBits.data());
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

static void BuildTrayIcons() {
    for (int i = 0; i < 3; ++i) {
        if (g_trayIcons[i]) DestroyIcon(g_trayIcons[i]);
        g_trayIcons[i] = MakeTrayIcon(i);
    }
}

static std::wstring TrayTip(int state) {
    int blank = 0, dim = 0;
    for (auto* o : g_overlays) {
        if (o->closing) continue;
        (o->mode == MODE_BLANK ? blank : dim)++;
    }
    if (state == TRAY_IDLE) {
        std::wstring hk = FormatHotkey(g_s.hkBlank);
        return std::wstring(kAppName) + (hk.empty() ? L"" : L" - " + hk + L" blanks all screens");
    }
    std::wstring s = kAppName;
    if (blank) s += L" - " + std::to_wstring(blank) + (blank == 1 ? L" screen blanked" : L" screens blanked");
    if (dim) s += L" - " + std::to_wstring(dim) + (dim == 1 ? L" screen dimmed" : L" screens dimmed");
    return s;
}

static void TrayFill(NOTIFYICONDATAW& nid) {
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_main;
    nid.uID = 1;
}

static void TrayAdd() {
    NOTIFYICONDATAW nid{};
    TrayFill(nid);
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    int state = g_trayState < 0 ? TRAY_IDLE : g_trayState;
    nid.hIcon = g_trayIcons[state];
    wcsncpy_s(nid.szTip, TrayTip(state).c_str(), _TRUNCATE);
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

static void TrayUpdate(int state) {
    g_trayState = state;
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    TrayFill(nid);
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = g_trayIcons[state];
    wcsncpy_s(nid.szTip, TrayTip(state).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void TrayRemove() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    TrayFill(nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static void Balloon(const std::wstring& title, const std::wstring& text) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    TrayFill(nid);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ---------------------------------------------------------------------------
// Input hooks (active only while a "blank all" session runs)
// ---------------------------------------------------------------------------

static UINT ModBit(DWORD vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return MOD_ALT;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return MOD_SHIFT;
    case VK_LWIN: case VK_RWIN: return MOD_WIN;
    }
    return 0;
}

static void RequestWake() { PostMessageW(g_main, WM_APP_WAKE, 0, 0); }

static LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if (UINT bit = ModBit(k->vkCode)) {
            if (down) g_modState |= bit; else g_modState &= ~bit;
        }
        const bool injected = (k->flags & LLKHF_INJECTED) != 0;
        // Key-ups always pass, so no key is ever left "stuck" for the app below.
        if (down && !(injected && g_s.ignoreInjected) && Now() >= g_graceUntil) {
            if (g_s.wakeMode == WAKE_HOTKEY) {
                if (k->vkCode == g_s.hkBlank.vk && g_modState == g_s.hkBlank.mods) RequestWake();
                return 1; // locked: nothing reaches the apps underneath
            }
            RequestWake();
            if (g_s.swallowWake) return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        const bool injected = (m->flags & LLMHF_INJECTED) != 0;
        if (!(injected && g_s.ignoreInjected)) {
            if (Now() < g_graceUntil) {
                g_anchor = m->pt;  // follow the hand until the grace period ends
            } else {
                switch (wp) {
                case WM_MOUSEMOVE: {
                    if (g_s.wakeMode == WAKE_ANY) {
                        long dx = m->pt.x - g_anchor.x, dy = m->pt.y - g_anchor.y;
                        long t = g_s.mouseThreshold;
                        if (dx * dx + dy * dy > t * t) RequestWake();
                    }
                    break;
                }
                case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
                case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                    if (g_s.wakeMode == WAKE_ANY) RequestWake();
                    if (g_s.wakeMode != WAKE_ANY || g_s.swallowWake) return 1;
                    break;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void InstallHooks() {
    if (g_kbHook) return;
    g_modState = 0;
    const DWORD mods[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN};
    for (DWORD vk : mods)
        if (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) g_modState |= ModBit(vk);
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, g_inst, 0);
    g_msHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, g_inst, 0);
}

static void RemoveHooks() {
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    if (g_msHook) UnhookWindowsHookEx(g_msHook);
    g_kbHook = g_msHook = nullptr;
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

static Overlay* FindOverlay(const std::wstring& device) {
    for (auto* o : g_overlays)
        if (_wcsicmp(o->device.c_str(), device.c_str()) == 0) return o;
    return nullptr;
}

static bool IsActive(const Overlay* o, int mode) { return o && !o->closing && o->mode == mode; }

static bool SessionActive() {
    for (auto* o : g_overlays)
        if (!o->closing && o->wakeOnInput) return true;
    return false;
}

static int TargetAlpha(int mode) { return mode == MODE_BLANK ? 255 : g_s.dimPercent * 255 / 100; }

static void SetClickThrough(HWND h, bool on) {
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    LONG_PTR want = on ? (ex | WS_EX_TRANSPARENT) : (ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    if (want != ex) SetWindowLongPtrW(h, GWL_EXSTYLE, want);
}

static void StartAnim() { SetTimer(g_main, TIMER_ANIM, 15, nullptr); }

static void ApplyAlpha(Overlay* o) {
    if (o->hwnd) SetLayeredWindowAttributes(o->hwnd, 0, static_cast<BYTE>(o->alpha), LWA_ALPHA);
}

static void ShowOverlay(const MonitorInfo& m, int mode, bool wakeOnInput, bool sleep = false,
                        int offMethod = OFF_AUTO) {
    if (m.detached) return;
    if (mode != MODE_BLANK) sleep = false;
    Overlay* o = FindOverlay(m.device);
    if (o && !o->closing && (o->sleep != sleep || o->offMethod != offMethod)) {
        // Switching between plain blank and "Off": undo the old DDC/CI changes, then redo.
        OverlayDdcRestore(o);
    }
    if (o) {
        o->sleep = sleep;
        o->offMethod = offMethod;
    }
    if (!o) {
        o = new Overlay;
        o->device = m.device;
        o->mode = mode;
        o->sleep = sleep;
        o->offMethod = offMethod;
        o->wakeOnInput = wakeOnInput;
        o->target = TargetAlpha(mode);
        o->alpha = g_s.fade ? 0 : o->target;
        DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
        if (mode == MODE_DIM) ex |= WS_EX_TRANSPARENT;
        o->hwnd = CreateWindowExW(ex, kOverlayClass, kAppName, WS_POPUP, m.rc.left, m.rc.top,
                                  m.rc.right - m.rc.left, m.rc.bottom - m.rc.top, nullptr, nullptr, g_inst, o);
        if (!o->hwnd) {
            delete o;
            return;
        }
        ApplyAlpha(o);
        ShowWindow(o->hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(o->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_overlays.push_back(o);
    } else {
        o->closing = false;
        o->mode = mode;
        o->wakeOnInput = wakeOnInput;
        o->target = TargetAlpha(mode);
        if (!g_s.fade) { o->alpha = o->target; ApplyAlpha(o); }
        SetClickThrough(o->hwnd, mode == MODE_DIM);
        SetWindowPos(o->hwnd, HWND_TOPMOST, m.rc.left, m.rc.top, m.rc.right - m.rc.left, m.rc.bottom - m.rc.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(o->hwnd, nullptr, TRUE);
    }
    if (mode == MODE_BLANK) OverlayDdcDark(o);
    else OverlayDdcRestore(o);
    StartAnim();
}

static void CloseOverlay(Overlay* o) {
    if (o->closing) return;
    o->closing = true;
    o->wakeOnInput = false;
    o->target = 0;
    OverlayDdcRestore(o);
    if (o->hwnd) {
        SetClickThrough(o->hwnd, true);  // fading out: let clicks through right away
        InvalidateRect(o->hwnd, nullptr, FALSE);
    }
    if (!g_s.fade) o->alpha = 0;
    StartAnim();
}

// Destroys overlays that finished fading out.
static void Sweep() {
    for (auto it = g_overlays.begin(); it != g_overlays.end();) {
        Overlay* o = *it;
        if (o->closing && o->alpha <= 0) {
            OverlayDdcRestore(o);
            if (o->hwnd) DestroyWindow(o->hwnd);
            delete o;
            it = g_overlays.erase(it);
        } else {
            ++it;
        }
    }
}

static void UpdateState() {
    bool anyBlank = false, anyDim = false;
    for (auto* o : g_overlays) {
        if (o->closing) continue;
        if (o->mode == MODE_BLANK) anyBlank = true; else anyDim = true;
    }
    if (SessionActive()) InstallHooks(); else RemoveHooks();
    SetThreadExecutionState(g_s.keepAwake && anyBlank ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
    if (!g_overlays.empty()) SetTimer(g_main, TIMER_TOPMOST, 1500, nullptr);
    else KillTimer(g_main, TIMER_TOPMOST);
    if (anyBlank && g_s.showClock) SetTimer(g_main, TIMER_CLOCK, 30000, nullptr);
    else KillTimer(g_main, TIMER_CLOCK);
    TrayUpdate(anyBlank ? TRAY_BLANK : anyDim ? TRAY_DIM : TRAY_IDLE);
    RefreshScreensWindow();
}

static void AnimTick() {
    int step = g_s.fade ? std::max(1, 255 * 15 / g_s.fadeMs) : 255;
    bool busy = false;
    for (auto* o : g_overlays) {
        if (o->alpha == o->target) continue;
        if (o->alpha < o->target) o->alpha = std::min(o->target, o->alpha + step);
        else o->alpha = std::max(o->target, o->alpha - step);
        ApplyAlpha(o);
        if (o->alpha != o->target) busy = true;
    }
    Sweep();
    if (busy) return;
    KillTimer(g_main, TIMER_ANIM);
    if (g_pendingPowerOff) {
        g_pendingPowerOff = false;
        if (SessionActive()) {
            PostMessageW(g_main, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
            g_monitorsPoweredOff = true;
        }
    }
}

// Windows only turns monitors back on for input; waking from a hotkey, the tray or the
// command line has to nudge it.
static void PowerMonitorsOn() {
    if (!g_monitorsPoweredOff) return;
    g_monitorsPoweredOff = false;
    SetThreadExecutionState(ES_DISPLAY_REQUIRED);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;  // zero-distance move: wakes displays, cursor stays put
    SendInput(1, &in, sizeof(in));
}

static void ReassertTopmost() {
    for (auto* o : g_overlays)
        if (o->hwnd && IsWindowVisible(o->hwnd))
            SetWindowPos(o->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    for (HWND h : g_identify)
        if (IsWindow(h))
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static void DrawClock(HDC dc, const RECT& rc, Overlay* o) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t text[32];
    int len = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, text, ARRAYSIZE(text));
    if (len <= 1) return;
    --len;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HFONT font = CreateFontW(-std::max(24, h / 14), 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                             L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text, len, &sz);
    if (o->clockPos.x < 0 || o->clockPos.x + sz.cx > w || o->clockPos.y + sz.cy > h) {
        o->clockPos.x = std::rand() % std::max(1L, w - sz.cx);
        o->clockPos.y = std::rand() % std::max(1L, h - sz.cy);
    }
    SetBkMode(dc, TRANSPARENT);
    int g = g_s.clockBrightness;
    SetTextColor(dc, RGB(g, g, g));
    TextOutW(dc, o->clockPos.x, o->clockPos.y, text, len);
    SelectObject(dc, old);
    DeleteObject(font);
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* o = reinterpret_cast<Overlay*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        if (o && !o->closing && o->mode == MODE_BLANK && g_s.hideCursor) SetCursor(nullptr);
        else SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;
    case WM_LBUTTONDBLCLK:
        // A screen blanked on its own (not part of "blank all") wakes on double-click.
        if (o && !o->wakeOnInput) PostMessageW(g_main, WM_APP_CLOSE_OVERLAY, 0, reinterpret_cast<LPARAM>(o));
        return 0;
    case WM_DPICHANGED:
        return 0;  // positioned in physical pixels; ignore the suggested rect
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (o && !o->closing && o->mode == MODE_BLANK && g_s.showClock) DrawClock(dc, rc, o);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Identify monitors
// ---------------------------------------------------------------------------

static LRESULT CALLBACK IdentifyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<IdentifyData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_NCDESTROY:
        delete d;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
    case WM_LBUTTONDOWN:
        DestroyWindow(h);
        return 0;
    case WM_DPICHANGED:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(18, 22, 30));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        if (d) {
            int H = rc.bottom - rc.top;
            SetBkMode(dc, TRANSPARENT);
            HFONT big = CreateFontW(-H * 55 / 100, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                    ANTIALIASED_QUALITY, 0, L"Segoe UI");
            HFONT smallFont = CreateFontW(-H * 9 / 100, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                      ANTIALIASED_QUALITY, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(dc, big);
            SetTextColor(dc, RGB(240, 244, 250));
            RECT top = rc;
            top.bottom = rc.top + H * 68 / 100;
            std::wstring num = std::to_wstring(d->number);
            DrawTextW(dc, num.c_str(), -1, &top, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, smallFont);
            SetTextColor(dc, RGB(170, 180, 195));
            RECT line1 = rc;
            line1.top = top.bottom;
            line1.bottom = line1.top + H * 14 / 100;
            DrawTextW(dc, d->name.c_str(), -1, &line1, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT line2 = rc;
            line2.top = line1.bottom;
            DrawTextW(dc, d->detail.c_str(), -1, &line2, DT_CENTER | DT_TOP | DT_SINGLELINE);
            SelectObject(dc, old);
            DeleteObject(big);
            DeleteObject(smallFont);
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void IdentifyMonitors() {
    for (HWND h : g_identify)
        if (IsWindow(h)) DestroyWindow(h);
    g_identify.clear();
    RefreshMonitors();
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        const auto& m = g_monitors[i];
        int mw = m.rc.right - m.rc.left, mh = m.rc.bottom - m.rc.top;
        int h = std::max(160, mh / 3);
        int w = std::min(mw - 40, h * 16 / 10);
        auto* d = new IdentifyData;
        d->number = static_cast<int>(i) + 1;
        d->name = m.name;
        d->detail = std::to_wstring(mw) + L" x " + std::to_wstring(mh) + (m.primary ? L"  -  primary" : L"");
        HWND win = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kIdentifyClass, kAppName,
                                   WS_POPUP, m.rc.left + (mw - w) / 2, m.rc.top + (mh - h) / 2, w, h, nullptr,
                                   nullptr, g_inst, d);
        if (!win) {
            delete d;
            continue;
        }
        ShowWindow(win, SW_SHOWNOACTIVATE);
        SetTimer(win, 1, 3000, nullptr);
        g_identify.push_back(win);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static void BlankAll(ULONGLONG graceMs) {
    RefreshMonitors();
    for (const auto& m : g_monitors) ShowOverlay(m, MODE_BLANK, true);  // skips screens that are off
    g_graceUntil = Now() + graceMs;
    GetCursorPos(&g_anchor);
    g_pendingPowerOff = g_s.powerOff;
    UpdateState();
}

// Turns one screen off with its own off method. Not a session: input doesn't wake it.
static void TurnOffMonitor(size_t index) {
    if (index >= g_monitors.size() || g_monitors[index].detached) return;
    const MonitorInfo m = g_monitors[index];
    int method = OffMethodFor(m.name);
    Log(L"off: screen " + std::to_wstring(index + 1) + L" " + m.name + L" (" + m.device + L") method " +
        std::to_wstring(method));
    if (method == OFF_CUT) {
        if (DetachMonitors({index})) {
            RefreshMonitors();
            UpdateState();
            return;
        }
        Balloon(kAppName, L"Couldn't cut the signal to " + m.name +
                              L" (at least one screen has to stay connected). Blanked it instead.");
        Log(L"off: cut failed, falling back to Auto");
        method = OFF_AUTO;
    }
    ShowOverlay(m, MODE_BLANK, false, true, method);
    Sweep();
    UpdateState();
}


static void WakeAll(bool byUser) {
    bool hadSession = SessionActive();
    ReattachMonitors();
    for (auto* o : g_overlays) CloseOverlay(o);
    RemoveHooks();  // before the nudge below, so it can't count as input
    PowerMonitorsOn();
    g_lastWake = Now();
    g_pendingPowerOff = false;
    Sweep();
    UpdateState();
    if (byUser && hadSession && g_s.lockOnWake) LockWorkStation();
}

// Called from the input hooks: ends only the "blank all" session.
static void WakeSession() {
    if (!SessionActive()) return;
    ReattachMonitors();
    for (auto* o : g_overlays)
        if (!o->closing && o->wakeOnInput) CloseOverlay(o);
    g_monitorsPoweredOff = false;  // woken by real input, Windows turns them on itself
    g_lastWake = Now();
    g_pendingPowerOff = false;
    Sweep();
    UpdateState();
    if (g_s.lockOnWake) LockWorkStation();
}

static void ToggleBlankAll(ULONGLONG graceMs) {
    RefreshMonitors();
    bool allBlank = !g_monitors.empty();
    for (const auto& m : g_monitors)
        if (!m.detached && !IsActive(FindOverlay(m.device), MODE_BLANK)) allBlank = false;
    if (SessionActive() || allBlank) WakeAll(true);
    else BlankAll(graceMs);
}

static void ToggleBlankOthers() {
    RefreshMonitors();
    if (g_monitors.size() < 2) {
        Balloon(kAppName, L"Only one screen is connected. Use Blank all screens instead.");
        return;
    }
    int cur = CursorMonitorIndex();
    bool already = true;
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        bool blanked = IsActive(FindOverlay(g_monitors[i].device), MODE_BLANK);
        if ((static_cast<int>(i) == cur) == blanked) already = false;
    }
    if (already) {
        WakeAll(true);
        return;
    }
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        if (static_cast<int>(i) == cur) {
            if (Overlay* o = FindOverlay(g_monitors[i].device)) CloseOverlay(o);
        } else {
            ShowOverlay(g_monitors[i], MODE_BLANK, false);
        }
    }
    Sweep();
    UpdateState();
}

static void ToggleMonitor(int index, int mode) {
    RefreshMonitors();
    if (index < 0 || index >= static_cast<int>(g_monitors.size())) return;
    const auto& m = g_monitors[index];
    Overlay* o = FindOverlay(m.device);
    if (IsActive(o, mode)) CloseOverlay(o);
    else ShowOverlay(m, mode, false);
    Sweep();
    UpdateState();
}

static const int kModeOff = 3;  // SetMonitorMode only: turn off with the screen's off method

// mode 0 = on (no overlay), MODE_DIM, MODE_BLANK or kModeOff.
static void SetMonitorMode(int index, int mode) {
    RefreshMonitors();
    if (index < 0 || index >= static_cast<int>(g_monitors.size())) { Log(L"set mode: bad screen index " + std::to_wstring(index)); return; }
    Log(L"set mode " + std::to_wstring(mode) + L" on screen " + std::to_wstring(index + 1) + L" " + g_monitors[index].name +
        (g_monitors[index].detached ? L" (signal cut)" : L""));
    if (g_monitors[index].detached) {
        std::wstring device = g_monitors[index].device;
        ReattachMonitors();
        RefreshMonitors();
        UpdateState();
        if (mode == 0) {
            DdcOptions on;
            on.power = true;
            DdcQueue(device, false, on);
        }
        if (mode == 0 || mode == kModeOff) return;
        index = -1;
        for (size_t i = 0; i < g_monitors.size(); ++i)
            if (_wcsicmp(g_monitors[i].device.c_str(), device.c_str()) == 0 && !g_monitors[i].detached)
                index = static_cast<int>(i);
        if (index < 0) return;
    }
    if (mode == kModeOff) {
        TurnOffMonitor(static_cast<size_t>(index));
        return;
    }
    const auto& m = g_monitors[index];
    Overlay* o = FindOverlay(m.device);
    if (mode == 0) {
        if (o) CloseOverlay(o);
        DdcOptions on;
        on.power = true;  // also wake it from standby, whoever put it there
        DdcQueue(m.device, false, on);
    } else if (!IsActive(o, mode) || (o->sleep && mode == MODE_BLANK)) {
        ShowOverlay(m, mode, false);
    }
    Sweep();
    UpdateState();
}

// Wakes everything screenBlank did and also asks every screen to leave standby.
static void PowerOnAll() {
    WakeAll(true);
    RefreshMonitors();
    DdcOptions on;
    on.power = true;
    for (const auto& m : g_monitors) DdcQueue(m.device, false, on);
}

static void ToggleDimAll() {
    RefreshMonitors();
    bool allDim = !g_monitors.empty();
    for (const auto& m : g_monitors)
        if (!m.detached && !IsActive(FindOverlay(m.device), MODE_DIM)) allDim = false;
    for (const auto& m : g_monitors) {
        if (m.detached) continue;
        Overlay* o = FindOverlay(m.device);
        if (allDim) { if (o) CloseOverlay(o); }
        else ShowOverlay(m, MODE_DIM, false);
    }
    Sweep();
    UpdateState();
}

static void OnDisplayChanged() {
    RefreshMonitors();
    for (auto* o : g_overlays) {
        if (!o->hwnd) continue;
        if (const MonitorInfo* m = FindMonitor(o->device)) {
            SetWindowPos(o->hwnd, HWND_TOPMOST, m->rc.left, m->rc.top, m->rc.right - m->rc.left,
                         m->rc.bottom - m->rc.top, SWP_NOACTIVATE | (o->closing ? 0 : SWP_SHOWWINDOW));
        } else {
            ShowWindow(o->hwnd, SW_HIDE);  // monitor gone (or asleep); keep the record for when it returns
        }
    }
    // A screen plugged in during "blank all" gets blanked too.
    if (SessionActive())
        for (const auto& m : g_monitors)
            if (!m.detached && !FindOverlay(m.device))
                ShowOverlay(m, MODE_BLANK, true);
    UpdateState();
}

static bool SomethingWantsDisplay() {
    QUERY_USER_NOTIFICATION_STATE q;
    if (SUCCEEDED(SHQueryUserNotificationState(&q)) &&
        (q == QUNS_BUSY || q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_PRESENTATION_MODE))
        return true;
    EXECUTION_STATE es = 0;
    if (CallNtPowerInformation(SystemExecutionState, nullptr, 0, &es, sizeof(es)) == 0 &&
        (es & ES_DISPLAY_REQUIRED))
        return true;  // e.g. a video player keeping the display on
    return false;
}

static void ApplySettings();

static void CheckIniChanged() {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(g_ini.c_str(), GetFileExInfoStandard, &fa)) return;
    if (CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) == 0) return;
    LoadSettings();
    ApplySettings();
    Balloon(kAppName, L"Settings reloaded.");
}

static void IdleTick() {
    CheckIniChanged();
    if (g_s.idleMinutes <= 0 || SessionActive()) return;
    LASTINPUTINFO li{sizeof(li)};
    if (!GetLastInputInfo(&li)) return;
    ULONGLONG idle = GetTickCount() - li.dwTime;
    // Wake input may have been swallowed and never counted, so measure from the wake too.
    idle = std::min(idle, Now() - g_lastWake);
    if (idle < static_cast<ULONGLONG>(g_s.idleMinutes) * 60000ULL) return;
    if (g_s.idleRespectBusy && SomethingWantsDisplay()) return;
    BlankAll(300);
}

static void RegisterHotkeys(bool report) {
    for (int id = HK_BLANK; id <= HK_DIM; ++id) UnregisterHotKey(g_main, id);
    if (g_hSettings) return;  // re-registered when the Settings window closes
    struct { int id; const Hotkey* hk; } list[] = {
        {HK_BLANK, &g_s.hkBlank}, {HK_OTHERS, &g_s.hkOthers}, {HK_CURSOR, &g_s.hkCursor}, {HK_DIM, &g_s.hkDim}};
    std::wstring failed;
    for (const auto& e : list) {
        if (!e.hk->vk) continue;
        if (!RegisterHotKey(g_main, e.id, e.hk->mods | MOD_NOREPEAT, e.hk->vk)) {
            if (!failed.empty()) failed += L", ";
            failed += FormatHotkey(*e.hk);
        }
    }
    if (report && !failed.empty())
        Balloon(L"Hotkey already in use", failed + L" is taken by another app. Pick another in settings.ini.");
}

static void ApplySettings() {
    RegisterHotkeys(true);
    for (auto* o : g_overlays) {
        if (o->closing) continue;
        o->target = TargetAlpha(o->mode);
        if (!g_s.fade) { o->alpha = o->target; ApplyAlpha(o); }
        InvalidateRect(o->hwnd, nullptr, TRUE);
    }
    StartAnim();
    UpdateState();
}

static void OpenSettingsFile() {
    EnsureIniExists();
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + g_ini + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

static void ShowAbout() {
    auto hk = [](const Hotkey& h) { std::wstring s = FormatHotkey(h); return s.empty() ? std::wstring(L"(off)") : s; };
    std::wstring text = std::wstring(kAppName) + L" " + kVersion +
        L"\n\nMakes screens look switched off, even when a monitor can't sleep.\n\n"
        L"Hotkeys\n"
        L"  " + hk(g_s.hkBlank) + L"\tBlank / wake all screens\n"
        L"  " + hk(g_s.hkOthers) + L"\tBlank all screens except the mouse's\n"
        L"  " + hk(g_s.hkCursor) + L"\tBlank / wake the mouse's screen\n"
        L"  " + hk(g_s.hkDim) + L"\tDim / undim all screens\n"
        L"\n"
        L"Double-click a screen blanked on its own to wake it.\n"
        L"Ctrl+Alt+Del always works, whatever the wake mode.\n\n"
        L"Settings file:\n" + g_ini;
    MessageBoxW(nullptr, text.c_str(), kAppName, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

static void ExitApp() {
    if (g_hScreens) DestroyWindow(g_hScreens);
    if (g_hSettings) DestroyWindow(g_hSettings);
    for (auto* o : g_overlays) {
        OverlayDdcRestore(o);  // the worker finishes these before the process exits
        if (o->hwnd) DestroyWindow(o->hwnd);
        delete o;
    }
    g_overlays.clear();
    if (!g_detached.empty()) {
        ReattachMonitors();
        Sleep(1500);  // no timers after this point; put windows back now
        RestoreWindows();
    }
    RemoveHooks();
    SetThreadExecutionState(ES_CONTINUOUS);
    TrayRemove();
    DestroyWindow(g_main);
}

// ---------------------------------------------------------------------------
// GUI: Screens window (live monitor map) and Settings window
// ---------------------------------------------------------------------------

static const wchar_t kMapClass[] = L"screenBlank.Map";
static const ULONGLONG kGuiGraceMs = 2000;  // hands off the mouse after clicking a button

enum ScreenState : int { ST_ON = 0, ST_DIM = 1, ST_BLANK = 2, ST_OFF = 3 };

// Short names for the Screens window's dropdown; long ones for the right-click menu.
static const int kOffMethods[] = {OFF_AUTO, OFF_HARD, OFF_CUT, OFF_BLACK};  // dropdown / menu order
static const wchar_t* const kOffMethodShort[] = {L"Auto", L"Power off", L"Cut signal", L"Black only"};
static const wchar_t* const kOffMethodNames[] = {
    L"Auto (its own standby if it has one, otherwise black)",
    L"Power off (light off too; some screens then need their power button)",
    L"Cut its signal (windows put back afterwards)",
    L"Black only (the monitor itself is never touched)",
};

static int OffMethodIndex(int method) {
    for (int k = 0; k < static_cast<int>(ARRAYSIZE(kOffMethods)); ++k)
        if (kOffMethods[k] == method) return k;
    return 0;
}

static UINT DpiOf(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT d = fn ? fn(h) : 96;
    return d ? d : 96;
}

static HFONT MakeUiFont(UINT dpi, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static int MonitorStateOf(size_t i, bool* backlightDown = nullptr) {
    if (g_monitors[i].detached) return ST_OFF;
    Overlay* o = FindOverlay(g_monitors[i].device);
    if (backlightDown) *backlightDown = o && !o->closing && o->ddcDark;
    if (IsActive(o, MODE_BLANK)) return o->sleep ? ST_OFF : ST_BLANK;
    if (IsActive(o, MODE_DIM)) return ST_DIM;
    return ST_ON;
}

static std::wstring StateText(size_t i) {
    bool dark = false;
    int st = MonitorStateOf(i, &dark);
    if (st == ST_OFF) {
        if (g_monitors[i].detached) return L"Off (signal cut)";
        Overlay* o = FindOverlay(g_monitors[i].device);
        int m = o ? o->offMethod : OFF_AUTO;
        return m == OFF_BLACK ? L"Off (black)" : m == OFF_HARD ? L"Powered off" : L"Off";
    }
    if (st == ST_BLANK) return L"Blanked";
    if (st == ST_DIM) return L"Dimmed " + std::to_wstring(g_s.dimPercent) + L"%";
    return L"On";
}

static void PaintMap(HWND h, HDC dc) {
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(30, 32, 36));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    g_mapRects.assign(g_monitors.size(), RECT{});
    if (g_monitors.empty()) return;

    RECT bounds = g_monitors[0].mapRc;  // remembered layout, so a cut screen keeps its place
    for (const auto& m : g_monitors) UnionRect(&bounds, &bounds, &m.mapRc);
    const UINT dpi = DpiOf(h);
    const int pad = MulDiv(14, static_cast<int>(dpi), 96);
    const int gap = MulDiv(4, static_cast<int>(dpi), 96);
    const double bw = std::max(1L, bounds.right - bounds.left);
    const double bh = std::max(1L, bounds.bottom - bounds.top);
    const double s = std::min((rc.right - 2.0 * pad) / bw, (rc.bottom - 2.0 * pad) / bh);
    const double ox = (rc.right - bw * s) / 2.0, oy = (rc.bottom - bh * s) / 2.0;

    HFONT big = MakeUiFont(dpi, 18, FW_SEMIBOLD);
    HFONT smallFont = MakeUiFont(dpi, 8, FW_NORMAL);
    HGDIOBJ oldFont = SelectObject(dc, big);
    SetBkMode(dc, TRANSPARENT);
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        const RECT& m = g_monitors[i].mapRc;
        RECT r{static_cast<LONG>(ox + (m.left - bounds.left) * s) + gap,
               static_cast<LONG>(oy + (m.top - bounds.top) * s) + gap,
               static_cast<LONG>(ox + (m.right - bounds.left) * s) - gap,
               static_cast<LONG>(oy + (m.bottom - bounds.top) * s) - gap};
        g_mapRects[i] = r;
        int st = MonitorStateOf(i);
        COLORREF fill = st == ST_ON ? RGB(47, 128, 237) : st == ST_DIM ? RGB(62, 70, 88) : RGB(0, 0, 0);
        COLORREF text = st == ST_ON    ? RGB(255, 255, 255)
                        : st == ST_DIM ? RGB(205, 210, 220)
                        : st == ST_OFF ? RGB(110, 110, 110)
                                       : RGB(150, 150, 150);
        const bool sel = static_cast<int>(i) == g_selMon;
        const int penW = sel ? MulDiv(3, static_cast<int>(dpi), 96) : 1;
        HPEN pen = CreatePen(PS_INSIDEFRAME, penW, sel ? RGB(255, 184, 0) : RGB(96, 102, 112));
        HBRUSH brush = CreateSolidBrush(fill);
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, brush);
        const int radius = MulDiv(8, static_cast<int>(dpi), 96);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
        DeleteObject(brush);

        SetTextColor(dc, text);
        RECT t = r;
        InflateRect(&t, -2 * gap, -2 * gap);
        const int mid = t.top + (t.bottom - t.top) * 55 / 100;
        RECT numR{t.left, t.top, t.right, mid};
        SelectObject(dc, big);
        std::wstring num = std::to_wstring(i + 1);
        DrawTextW(dc, num.c_str(), -1, &numR, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        SelectObject(dc, smallFont);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        RECT l1{t.left, mid + gap, t.right, mid + gap + tm.tmHeight};
        DrawTextW(dc, g_monitors[i].name.c_str(), -1, &l1, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT l2{t.left, l1.bottom, t.right, l1.bottom + tm.tmHeight};
        std::wstring state = StateText(i);
        DrawTextW(dc, state.c_str(), -1, &l2, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    DeleteObject(big);
    DeleteObject(smallFont);

    if (GetFocus() == h) {
        RECT f = rc;
        InflateRect(&f, -2, -2);
        DrawFocusRect(dc, &f);
    }
}

static int MapHit(POINT pt) {
    for (size_t i = 0; i < g_mapRects.size(); ++i)
        if (PtInRect(&g_mapRects[i], pt)) return static_cast<int>(i);
    return -1;
}

static LRESULT CALLBACK MapProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintMap(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        int i = MapHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (i >= 0) {
            g_selMon = i;
            RefreshScreensWindow();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int i = MapHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (i >= 0) {
            g_selMon = i;
            SetMonitorMode(i, MonitorStateOf(static_cast<size_t>(i)) == ST_BLANK ? 0 : MODE_BLANK);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        int i = MapHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (i < 0) return 0;
        g_selMon = i;
        RefreshScreensWindow();
        const size_t idx = static_cast<size_t>(i);
        const int st = MonitorStateOf(idx);
        const std::wstring name = g_monitors[idx].name;
        const int method = OffMethodFor(name);
        enum : UINT { C_ON = 1, C_DIM, C_BLANK, C_OFF, C_POWER, C_IDENTIFY, C_METHOD = 20 };
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, (std::to_wstring(i + 1) + L"  " + MenuEscape(name)).c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (st == ST_ON ? MF_GRAYED : 0), C_ON, L"On");
        AppendMenuW(m, MF_STRING | (st == ST_DIM ? MF_CHECKED : 0), C_DIM, L"Dim");
        AppendMenuW(m, MF_STRING | (st == ST_BLANK ? MF_CHECKED : 0), C_BLANK, L"Blank");
        AppendMenuW(m, MF_STRING | (st == ST_OFF ? MF_CHECKED : 0), C_OFF, L"Off");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, C_POWER, L"Power on (wake it from standby)");
        HMENU sub = CreatePopupMenu();
        for (int k = 0; k < static_cast<int>(ARRAYSIZE(kOffMethods)); ++k)
            AppendMenuW(sub, MF_STRING | MFT_RADIOCHECK | (kOffMethods[k] == method ? MF_CHECKED : 0), C_METHOD + k,
                        kOffMethodNames[k]);
        AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Turn off by");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, C_IDENTIFY, L"Identify screens");
        POINT pt;
        GetCursorPos(&pt);
        UINT cmd = static_cast<UINT>(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr));
        DestroyMenu(m);
        switch (cmd) {
        case C_ON:       SetMonitorMode(i, 0); break;
        case C_DIM:      SetMonitorMode(i, st == ST_DIM ? 0 : MODE_DIM); break;
        case C_BLANK:    SetMonitorMode(i, st == ST_BLANK ? 0 : MODE_BLANK); break;
        case C_OFF:      SetMonitorMode(i, st == ST_OFF ? 0 : kModeOff); break;
        case C_POWER:    SetMonitorMode(i, 0); break;  // On also sends DDC/CI power-on
        case C_IDENTIFY: IdentifyMonitors(); break;
        default:
            if (cmd >= C_METHOD && cmd < C_METHOD + ARRAYSIZE(kOffMethods)) {
                SetOffMethodFor(name, kOffMethods[cmd - C_METHOD]);
                RefreshScreensWindow();
            }
        }
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN: {
        int n = static_cast<int>(g_monitors.size());
        if (!n) return 0;
        if (wp == VK_LEFT || wp == VK_UP) g_selMon = (g_selMon + n - 1) % n;
        else if (wp == VK_RIGHT || wp == VK_DOWN) g_selMon = (g_selMon + 1) % n;
        else if (wp == VK_SPACE) {
            SetMonitorMode(g_selMon, MonitorStateOf(static_cast<size_t>(g_selMon)) == ST_BLANK ? 0 : MODE_BLANK);
            return 0;
        } else return 0;
        RefreshScreensWindow();
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void RefreshScreensWindow() {
    if (!g_hScreens) return;
    if (g_monitors.empty()) RefreshMonitors();
    if (g_selMon < 0 || g_selMon >= static_cast<int>(g_monitors.size())) g_selMon = 0;
    if (g_hMap) InvalidateRect(g_hMap, nullptr, FALSE);

    std::wstring label = L"No screens found";
    int st = -1, on = 0, dim = 0, blank = 0, off = 0;
    for (size_t i = 0; i < g_monitors.size(); ++i) {
        int s = MonitorStateOf(i);
        (s == ST_ON ? on : s == ST_DIM ? dim : s == ST_OFF ? off : blank)++;
    }
    if (!g_monitors.empty()) {
        size_t i = static_cast<size_t>(g_selMon);
        const auto& m = g_monitors[i];
        st = MonitorStateOf(i);
        label = L"Screen " + std::to_wstring(i + 1) + L"  ·  " + m.name + L"  ·  " +
                std::to_wstring(m.rc.right - m.rc.left) + L"x" + std::to_wstring(m.rc.bottom - m.rc.top) +
                (m.primary ? L", primary" : L"") + L"  ·  " + StateText(i);
    }
    SetDlgItemTextW(g_hScreens, IDC_SEL_LABEL, label.c_str());
    EnableWindow(GetDlgItem(g_hScreens, IDC_SEL_ON), st >= 0 && st != ST_ON);
    EnableWindow(GetDlgItem(g_hScreens, IDC_SEL_DIM), st >= 0 && st != ST_DIM);
    EnableWindow(GetDlgItem(g_hScreens, IDC_SEL_BLANK), st >= 0 && st != ST_BLANK);
    EnableWindow(GetDlgItem(g_hScreens, IDC_SEL_OFF), st >= 0 && st != ST_OFF);
    EnableWindow(GetDlgItem(g_hScreens, IDC_ALL_WAKE), dim + blank + off > 0);

    if (!g_monitors.empty()) {
        int method = OffMethodFor(g_monitors[static_cast<size_t>(g_selMon)].name);
        HWND combo = GetDlgItem(g_hScreens, IDC_OFF_METHOD);
        if (!SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) SendMessageW(combo, CB_SETCURSEL, OffMethodIndex(method), 0);
    }

    std::wstring status = std::to_wstring(on) + L" on";
    if (dim) status += L"  ·  " + std::to_wstring(dim) + L" dimmed";
    if (blank) status += L"  ·  " + std::to_wstring(blank) + L" blanked";
    if (off) status += L"  ·  " + std::to_wstring(off) + L" off";
    status += L"   (right-click a screen for more)";
    SetDlgItemTextW(g_hScreens, IDC_STATUS, status.c_str());

    std::wstring pct = std::to_wstring(g_s.dimPercent) + L"%";
    SetDlgItemTextW(g_hScreens, IDC_DIM_VALUE, pct.c_str());
}

static void ShowSettingsWindow();

static INT_PTR CALLBACK ScreensProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_hScreens = h;
        g_hMap = GetDlgItem(h, IDC_MAP);
        SendMessageW(h, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_appIcon));
        SendMessageW(h, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_appIcon));
        HWND slider = GetDlgItem(h, IDC_DIM_SLIDER);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 95));
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(slider, TBM_SETPOS, TRUE, g_s.dimPercent);
        HWND combo = GetDlgItem(h, IDC_OFF_METHOD);
        for (const wchar_t* name : kOffMethodShort) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        RefreshMonitors();
        g_selMon = CursorMonitorIndex();
        RefreshScreensWindow();
        return TRUE;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == GetDlgItem(h, IDC_DIM_SLIDER)) {
            int pos = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp), TBM_GETPOS, 0, 0));
            g_s.dimPercent = std::clamp(pos, 10, 95);
            for (auto* o : g_overlays) {
                if (o->closing || o->mode != MODE_DIM) continue;
                o->target = TargetAlpha(MODE_DIM);
                o->alpha = o->target;  // follow the slider directly
                ApplyAlpha(o);
            }
            RefreshScreensWindow();
            if (LOWORD(wp) == TB_ENDTRACK) SaveSettings();
        }
        return TRUE;
    case WM_DISPLAYCHANGE:
        // Can arrive in the middle of screenBlank's own SetDisplayConfig call; let the main
        // window pick it up once the layout has settled instead of refreshing here.
        SetTimer(g_main, TIMER_DISPLAY, 600, nullptr);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) Log(L"screens window button " + std::to_wstring(LOWORD(wp)) + L", selected screen " + std::to_wstring(g_selMon + 1));
        switch (LOWORD(wp)) {
        case IDC_SEL_ON:    SetMonitorMode(g_selMon, 0); break;
        case IDC_SEL_DIM:   SetMonitorMode(g_selMon, MODE_DIM); break;
        case IDC_SEL_BLANK: SetMonitorMode(g_selMon, MODE_BLANK); break;
        case IDC_SEL_OFF:   SetMonitorMode(g_selMon, kModeOff); break;
        case IDC_OFF_METHOD:
            if (HIWORD(wp) == CBN_SELCHANGE && g_selMon >= 0 && g_selMon < static_cast<int>(g_monitors.size())) {
                int sel = static_cast<int>(SendDlgItemMessageW(h, IDC_OFF_METHOD, CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(ARRAYSIZE(kOffMethods))) {
                    SetOffMethodFor(g_monitors[static_cast<size_t>(g_selMon)].name, kOffMethods[sel]);
                    RefreshScreensWindow();
                }
            }
            break;
        case IDC_IDENTIFY:  IdentifyMonitors(); break;
        case IDC_ALL_BLANK: if (!SessionActive()) BlankAll(kGuiGraceMs); break;
        case IDC_ALL_DIM:   ToggleDimAll(); break;
        case IDC_ALL_WAKE:  WakeAll(true); break;
        case IDC_OPEN_SETTINGS: ShowSettingsWindow(); break;
        case IDCANCEL:      DestroyWindow(h); break;
        }
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(h);
        return TRUE;
    case WM_DESTROY:
        g_hScreens = nullptr;
        g_hMap = nullptr;
        return TRUE;
    }
    return FALSE;
}

static void ShowScreensWindow() {
    if (!g_hScreens) CreateDialogParamW(g_inst, MAKEINTRESOURCEW(IDD_SCREENS), nullptr, ScreensProc, 0);
    if (!g_hScreens) return;
    ShowWindow(g_hScreens, IsIconic(g_hScreens) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hScreens);
}

// --- Settings window ---

static bool IsExtendedVk(UINT vk) {
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        return true;
    }
    return false;
}

static WORD ToHotkeyWord(const Hotkey& h) {
    if (!h.vk) return 0;
    BYTE f = 0;
    if (h.mods & MOD_SHIFT) f |= HOTKEYF_SHIFT;
    if (h.mods & MOD_CONTROL) f |= HOTKEYF_CONTROL;
    if (h.mods & MOD_ALT) f |= HOTKEYF_ALT;
    if (IsExtendedVk(h.vk)) f |= HOTKEYF_EXT;
    return MAKEWORD(static_cast<BYTE>(h.vk), f);
}

static Hotkey FromHotkeyWord(WORD w) {
    Hotkey h;
    h.vk = LOBYTE(w);
    if (!h.vk) return Hotkey{};
    BYTE f = HIBYTE(w);
    if (f & HOTKEYF_SHIFT) h.mods |= MOD_SHIFT;
    if (f & HOTKEYF_CONTROL) h.mods |= MOD_CONTROL;
    if (f & HOTKEYF_ALT) h.mods |= MOD_ALT;
    return h;
}

struct HotkeyBinding { int ctrl; Hotkey Settings::*field; const wchar_t* name; };
static const HotkeyBinding kHotkeyBindings[4] = {
    {IDC_HK_BLANK, &Settings::hkBlank, L"Blank / wake all"},
    {IDC_HK_OTHERS, &Settings::hkOthers, L"Blank all but this one"},
    {IDC_HK_CURSOR, &Settings::hkCursor, L"Blank this screen"},
    {IDC_HK_DIM, &Settings::hkDim, L"Dim all"},
};

static void SetCheck(HWND h, int id, bool on) { CheckDlgButton(h, id, on ? BST_CHECKED : BST_UNCHECKED); }
static bool GetCheck(HWND h, int id) { return IsDlgButtonChecked(h, id) == BST_CHECKED; }

static bool g_settingsLoading;  // filling the controls; their change notifications aren't edits

static void SettingsLoad(HWND h) {
    g_settingsLoading = true;
    for (int i = 0; i < 4; ++i) {
        HWND c = GetDlgItem(h, kHotkeyBindings[i].ctrl);
        WORD w = ToHotkeyWord(g_s.*(kHotkeyBindings[i].field));
        g_hkShown[i] = w;
        // A bare key or Shift+key is too easy to hit by accident; the control turns it into Ctrl+Alt+key.
        SendMessageW(c, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S, MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_ALT, 0));
        SendMessageW(c, HKM_SETHOTKEY, w, 0);
    }
    HWND wake = GetDlgItem(h, IDC_WAKE);
    SendMessageW(wake, CB_RESETCONTENT, 0, 0);
    SendMessageW(wake, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Any key, click or mouse move"));
    SendMessageW(wake, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Keyboard only"));
    SendMessageW(wake, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hotkey only (blocks other input)"));
    SendMessageW(wake, CB_SETCURSEL, g_s.wakeMode, 0);
    SetDlgItemInt(h, IDC_THRESHOLD, static_cast<UINT>(g_s.mouseThreshold), FALSE);
    SetCheck(h, IDC_SWALLOW, g_s.swallowWake);
    SetCheck(h, IDC_INJECTED, g_s.ignoreInjected);
    SetCheck(h, IDC_LOCK, g_s.lockOnWake);

    HWND idle = GetDlgItem(h, IDC_IDLE);
    SendMessageW(idle, CB_RESETCONTENT, 0, 0);
    bool found = false;
    auto addIdle = [&](int v) {
        std::wstring label = v == 0 ? L"Never" : std::to_wstring(v) + (v == 1 ? L" minute" : L" minutes");
        LRESULT idx = SendMessageW(idle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(idle, CB_SETITEMDATA, idx, v);
        if (v == g_s.idleMinutes) { SendMessageW(idle, CB_SETCURSEL, idx, 0); found = true; }
    };
    for (int v : kIdleMinutes) addIdle(v);
    if (!found) addIdle(g_s.idleMinutes);  // custom value from settings.ini
    SetCheck(h, IDC_RESPECT, g_s.idleRespectBusy);

    SetCheck(h, IDC_FADE, g_s.fade);
    SetCheck(h, IDC_HIDECURSOR, g_s.hideCursor);
    SetCheck(h, IDC_CLOCK, g_s.showClock);
    SetCheck(h, IDC_AWAKE, g_s.keepAwake);
    SetCheck(h, IDC_POWEROFF, g_s.powerOff);
    SetCheck(h, IDC_DDC, g_s.ddc);
    SetCheck(h, IDC_AUTOSTART, AutostartEnabled());
    SetCheck(h, IDC_EXPLORER_MENU, ExplorerMenuInstalled());
    g_settingsLoading = false;
    EnableWindow(GetDlgItem(h, IDC_APPLY), FALSE);  // nothing changed yet
}

static bool SettingsSave(HWND h) {
    Settings s = g_s;
    for (int i = 0; i < 4; ++i) {
        WORD w = static_cast<WORD>(SendDlgItemMessageW(h, kHotkeyBindings[i].ctrl, HKM_GETHOTKEY, 0, 0));
        // Unchanged boxes keep the exact hotkey from settings.ini (the control can't show Win+).
        if (w != g_hkShown[i]) s.*(kHotkeyBindings[i].field) = FromHotkeyWord(w);
    }
    for (int i = 0; i < 4; ++i) {
        const Hotkey& a = s.*(kHotkeyBindings[i].field);
        for (int j = i + 1; j < 4 && a.vk; ++j) {
            const Hotkey& b = s.*(kHotkeyBindings[j].field);
            if (a.vk == b.vk && a.mods == b.mods) {
                std::wstring msg = std::wstring(L"\"") + kHotkeyBindings[i].name + L"\" and \"" +
                                   kHotkeyBindings[j].name + L"\" use the same hotkey (" + FormatHotkey(a) + L").";
                MessageBoxW(h, msg.c_str(), kAppName, MB_OK | MB_ICONWARNING);
                return false;
            }
        }
    }
    s.wakeMode = static_cast<int>(SendDlgItemMessageW(h, IDC_WAKE, CB_GETCURSEL, 0, 0));
    if (s.wakeMode < WAKE_ANY || s.wakeMode > WAKE_HOTKEY) s.wakeMode = WAKE_ANY;
    if (s.wakeMode == WAKE_HOTKEY && !s.hkBlank.vk) {
        MessageBoxW(h, L"\"Hotkey only\" wake needs a Blank / wake all hotkey.", kAppName, MB_OK | MB_ICONWARNING);
        return false;
    }
    BOOL ok = FALSE;
    UINT t = GetDlgItemInt(h, IDC_THRESHOLD, &ok, FALSE);
    if (ok) s.mouseThreshold = std::clamp(static_cast<int>(t), 1, 2000);
    s.swallowWake = GetCheck(h, IDC_SWALLOW);
    s.ignoreInjected = GetCheck(h, IDC_INJECTED);
    s.lockOnWake = GetCheck(h, IDC_LOCK);
    LRESULT idleSel = SendDlgItemMessageW(h, IDC_IDLE, CB_GETCURSEL, 0, 0);
    if (idleSel != CB_ERR) s.idleMinutes = static_cast<int>(SendDlgItemMessageW(h, IDC_IDLE, CB_GETITEMDATA, idleSel, 0));
    s.idleRespectBusy = GetCheck(h, IDC_RESPECT);
    s.fade = GetCheck(h, IDC_FADE);
    s.hideCursor = GetCheck(h, IDC_HIDECURSOR);
    s.showClock = GetCheck(h, IDC_CLOCK);
    s.keepAwake = GetCheck(h, IDC_AWAKE);
    s.powerOff = GetCheck(h, IDC_POWEROFF);
    s.ddc = GetCheck(h, IDC_DDC);

    g_s = s;
    SaveSettings();
    if (GetCheck(h, IDC_AUTOSTART) != AutostartEnabled()) SetAutostart(GetCheck(h, IDC_AUTOSTART));
    if (GetCheck(h, IDC_EXPLORER_MENU) != ExplorerMenuInstalled()) SetExplorerMenu(GetCheck(h, IDC_EXPLORER_MENU));
    ApplySettings();
    SettingsLoad(h);
    return true;
}

static INT_PTR CALLBACK SettingsProc(HWND h, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        g_hSettings = h;
        SendMessageW(h, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_appIcon));
        SendMessageW(h, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_appIcon));
        // Global hotkeys would swallow the keys typed into the hotkey boxes.
        RegisterHotkeys(false);
        SettingsLoad(h);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:         if (SettingsSave(h)) DestroyWindow(h); break;
        case IDC_APPLY:    SettingsSave(h); break;
        case IDCANCEL:     DestroyWindow(h); break;
        case IDC_OPEN_INI: OpenSettingsFile(); break;
        default: {
            // Any edit (checkbox, dropdown, number or hotkey box) makes Apply available.
            const UINT code = HIWORD(wp);
            if (!g_settingsLoading && (code == BN_CLICKED || code == CBN_SELCHANGE || code == EN_CHANGE))
                EnableWindow(GetDlgItem(h, IDC_APPLY), TRUE);
        }
        }
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(h);
        return TRUE;
    case WM_DESTROY:
        g_hSettings = nullptr;
        RegisterHotkeys(true);
        return TRUE;
    }
    return FALSE;
}

static void ShowSettingsWindow() {
    if (!g_hSettings) CreateDialogParamW(g_inst, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, SettingsProc, 0);
    if (!g_hSettings) return;
    ShowWindow(g_hSettings, IsIconic(g_hSettings) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hSettings);
}

// ---------------------------------------------------------------------------
// Command line / second instance commands
// ---------------------------------------------------------------------------

static const wchar_t kUsage[] =
    L"screenBlank [command]\n\n"
    L"  (none)          start in the tray\n"
    L"  blank           blank all screens\n"
    L"  wake            wake all screens\n"
    L"  toggle          blank or wake all screens\n"
    L"  others          blank all screens except the mouse's\n"
    L"  cursor          blank or wake the mouse's screen\n"
    L"  monitor N       blank or wake screen N\n"
    L"  dim             dim or undim all screens\n"
    L"  dim-monitor N   dim or undim screen N\n"
    L"  off-monitor N   turn screen N off with its off method\n"
    L"  on-monitor N    turn screen N back on\n"
    L"  off-cursor      turn the mouse's screen off\n"
    L"  on-all          wake everything and power every screen on\n"
    L"  install-menu    add the Explorer right-click menu\n"
    L"  uninstall-menu  remove it\n"
    L"  identify        show each screen's number\n"
    L"  screens         open the Screens window\n"
    L"  settings        open the Settings window\n"
    L"  settings-file   open settings.ini\n"
    L"  exit            quit the running screenBlank\n\n"
    L"Commands go to the running instance; if none runs, one starts.";

static void RunCommand(const std::wstring& raw) {
    std::wstring cmd = Lower(Trim(raw));
    while (!cmd.empty() && (cmd[0] == L'-' || cmd[0] == L'/')) cmd.erase(0, 1);
    std::wstring verb = cmd, arg;
    size_t sp = cmd.find_first_of(L" \t");
    if (sp != std::wstring::npos) {
        verb = cmd.substr(0, sp);
        arg = Trim(cmd.substr(sp + 1));
    }
    const ULONGLONG cliGrace = std::max<ULONGLONG>(g_s.graceMs, 1500);
    if (verb.empty()) return;
    Log(L"command: " + cmd);
    if (verb == L"blank" || verb == L"off") { if (!SessionActive()) BlankAll(cliGrace); }
    else if (verb == L"wake" || verb == L"on") WakeAll(true);
    else if (verb == L"toggle") ToggleBlankAll(cliGrace);
    else if (verb == L"others" || verb == L"focus") ToggleBlankOthers();
    else if (verb == L"cursor") ToggleMonitor((RefreshMonitors(), CursorMonitorIndex()), MODE_BLANK);
    else if (verb == L"monitor") ToggleMonitor(_wtoi(arg.c_str()) - 1, MODE_BLANK);
    else if (verb == L"dim") ToggleDimAll();
    else if (verb == L"dim-monitor") ToggleMonitor(_wtoi(arg.c_str()) - 1, MODE_DIM);
    else if (verb == L"off-monitor") SetMonitorMode(_wtoi(arg.c_str()) - 1, kModeOff);
    else if (verb == L"off-cursor") SetMonitorMode((RefreshMonitors(), CursorMonitorIndex()), kModeOff);
    else if (verb == L"on-all") PowerOnAll();
    else if (verb == L"on-monitor") SetMonitorMode(_wtoi(arg.c_str()) - 1, 0);
    else if (verb == L"identify") IdentifyMonitors();
    else if (verb == L"screens") ShowScreensWindow();
    else if (verb == L"settings") ShowSettingsWindow();
    else if (verb == L"settings-file") OpenSettingsFile();
    else if (verb == L"about") ShowAbout();
    else if (verb == L"exit" || verb == L"quit") ExitApp();
    else MessageBoxW(nullptr, kUsage, kAppName, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

// ---------------------------------------------------------------------------
// Tray menu
// ---------------------------------------------------------------------------

static void AddItem(HMENU m, UINT id, const std::wstring& text, bool checked = false, bool enabled = true) {
    UINT flags = MF_STRING | (checked ? MF_CHECKED : 0) | (enabled ? 0 : MF_GRAYED);
    AppendMenuW(m, flags, id, text.c_str());
}

static std::wstring WithHotkey(const wchar_t* text, const Hotkey& h) {
    std::wstring hk = FormatHotkey(h);
    return hk.empty() ? std::wstring(text) : std::wstring(text) + L"\t" + hk;
}

static void ShowTrayMenu(POINT pt) {
    RefreshMonitors();
    HMENU menu = CreatePopupMenu();
    bool anything = false;
    for (auto* o : g_overlays) if (!o->closing) anything = true;

    AddItem(menu, ID_SCREENS_WINDOW, L"Screens...");
    AddItem(menu, ID_SETTINGS_WINDOW, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddItem(menu, ID_BLANK_ALL, WithHotkey(SessionActive() ? L"Wake all screens" : L"Blank all screens", g_s.hkBlank));
    AddItem(menu, ID_BLANK_OTHERS, WithHotkey(L"Blank all except this screen", g_s.hkOthers), false,
            g_monitors.size() > 1);
    AddItem(menu, ID_BLANK_CURSOR, WithHotkey(L"Blank this screen", g_s.hkCursor));
    AddItem(menu, ID_DIM_ALL, WithHotkey(L"Dim all screens", g_s.hkDim));
    if (anything) AddItem(menu, ID_WAKE_ALL, L"Wake everything");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU blankMon = CreatePopupMenu();
    HMENU dimMon = CreatePopupMenu();
    for (size_t i = 0; i < g_monitors.size() && i < 100; ++i) {
        Overlay* o = FindOverlay(g_monitors[i].device);
        AddItem(blankMon, ID_MON_BLANK_BASE + static_cast<UINT>(i), MonitorLabel(i), IsActive(o, MODE_BLANK));
        AddItem(dimMon, ID_MON_DIM_BASE + static_cast<UINT>(i), MonitorLabel(i), IsActive(o, MODE_DIM));
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(blankMon), L"Blank screen");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(dimMon), L"Dim screen");
    AddItem(menu, ID_IDENTIFY, L"Identify screens");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU wake = CreatePopupMenu();
    AddItem(wake, ID_WAKE_ANY, L"Any key, click or mouse move", g_s.wakeMode == WAKE_ANY);
    AddItem(wake, ID_WAKE_KEYBOARD, L"Keyboard only", g_s.wakeMode == WAKE_KEYBOARD);
    AddItem(wake, ID_WAKE_HOTKEY, WithHotkey(L"Hotkey only (blocks other input)", g_s.hkBlank),
            g_s.wakeMode == WAKE_HOTKEY, g_s.hkBlank.vk != 0);
    AppendMenuW(wake, MF_SEPARATOR, 0, nullptr);
    AddItem(wake, ID_OPT_SWALLOW, L"Don't pass the wake key/click to apps", g_s.swallowWake);
    AddItem(wake, ID_OPT_INJECTED, L"Ignore mouse jigglers / synthetic input", g_s.ignoreInjected);
    AddItem(wake, ID_OPT_LOCK, L"Lock Windows on wake", g_s.lockOnWake);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(wake), L"Wake on");

    HMENU dim = CreatePopupMenu();
    for (size_t i = 0; i < ARRAYSIZE(kDimLevels); ++i)
        AddItem(dim, ID_DIM_LEVEL_BASE + static_cast<UINT>(i), std::to_wstring(kDimLevels[i]) + L"% darker",
                g_s.dimPercent == kDimLevels[i]);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(dim), L"Dim strength");

    HMENU idle = CreatePopupMenu();
    for (size_t i = 0; i < ARRAYSIZE(kIdleMinutes); ++i) {
        int v = kIdleMinutes[i];
        std::wstring label = v == 0 ? L"Off" : std::to_wstring(v) + (v == 1 ? L" minute" : L" minutes");
        AddItem(idle, ID_IDLE_BASE + static_cast<UINT>(i), label, g_s.idleMinutes == v);
    }
    AppendMenuW(idle, MF_SEPARATOR, 0, nullptr);
    AddItem(idle, ID_OPT_RESPECT, L"Not while video or fullscreen apps run", g_s.idleRespectBusy);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(idle), L"Auto-blank when idle");

    HMENU opts = CreatePopupMenu();
    AddItem(opts, ID_OPT_FADE, L"Fade in and out", g_s.fade);
    AddItem(opts, ID_OPT_CURSOR, L"Hide mouse cursor on blank screens", g_s.hideCursor);
    AddItem(opts, ID_OPT_CLOCK, L"Show faint moving clock", g_s.showClock);
    AddItem(opts, ID_OPT_AWAKE, L"Keep PC awake while blanked", g_s.keepAwake);
    AppendMenuW(opts, MF_SEPARATOR, 0, nullptr);
    AddItem(opts, ID_OPT_POWEROFF, L"Also power monitors off (Blank all)", g_s.powerOff);
    AddItem(opts, ID_OPT_DDC, L"DDC/CI standby per monitor (experimental)", g_s.ddc);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opts), L"Options");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AddItem(menu, ID_AUTOSTART, L"Start with Windows", AutostartEnabled());
    AddItem(menu, ID_EDIT_SETTINGS, L"Edit settings file...");
    AddItem(menu, ID_ABOUT, L"About and hotkeys");
    AddItem(menu, ID_EXIT, L"Exit");
    SetMenuDefaultItem(menu, ID_SCREENS_WINDOW, FALSE);

    SetForegroundWindow(g_main);
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align,
                                                pt.x, pt.y, 0, g_main, nullptr));
    PostMessageW(g_main, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd) PostMessageW(g_main, WM_COMMAND, cmd, 0);
}

static void ToggleOpt(bool& opt) {
    opt = !opt;
    SaveSettings();
    ApplySettings();
}

static void OnCommand(UINT id) {
    const ULONGLONG menuGrace = std::max<ULONGLONG>(g_s.graceMs, 1500);
    if (id >= ID_MON_BLANK_BASE && id < ID_MON_BLANK_BASE + 100) { ToggleMonitor(id - ID_MON_BLANK_BASE, MODE_BLANK); return; }
    if (id >= ID_MON_DIM_BASE && id < ID_MON_DIM_BASE + 100) { ToggleMonitor(id - ID_MON_DIM_BASE, MODE_DIM); return; }
    if (id >= ID_DIM_LEVEL_BASE && id < ID_DIM_LEVEL_BASE + ARRAYSIZE(kDimLevels)) {
        g_s.dimPercent = kDimLevels[id - ID_DIM_LEVEL_BASE];
        SaveSettings();
        ApplySettings();
        return;
    }
    if (id >= ID_IDLE_BASE && id < ID_IDLE_BASE + ARRAYSIZE(kIdleMinutes)) {
        g_s.idleMinutes = kIdleMinutes[id - ID_IDLE_BASE];
        SaveSettings();
        return;
    }
    switch (id) {
    case ID_BLANK_ALL:     ToggleBlankAll(menuGrace); break;
    case ID_BLANK_OTHERS:  ToggleBlankOthers(); break;
    case ID_BLANK_CURSOR:  RefreshMonitors(); ToggleMonitor(CursorMonitorIndex(), MODE_BLANK); break;
    case ID_DIM_ALL:       ToggleDimAll(); break;
    case ID_WAKE_ALL:      WakeAll(true); break;
    case ID_IDENTIFY:      IdentifyMonitors(); break;
    case ID_WAKE_ANY:      g_s.wakeMode = WAKE_ANY; SaveSettings(); break;
    case ID_WAKE_KEYBOARD: g_s.wakeMode = WAKE_KEYBOARD; SaveSettings(); break;
    case ID_WAKE_HOTKEY:
        g_s.wakeMode = WAKE_HOTKEY;
        SaveSettings();
        Balloon(L"Hotkey-only wake", L"While all screens are blanked, keys and clicks are blocked. Only " +
                FormatHotkey(g_s.hkBlank) + L" wakes them (Ctrl+Alt+Del always works).");
        break;
    case ID_OPT_SWALLOW:   ToggleOpt(g_s.swallowWake); break;
    case ID_OPT_INJECTED:  ToggleOpt(g_s.ignoreInjected); break;
    case ID_OPT_LOCK:      ToggleOpt(g_s.lockOnWake); break;
    case ID_OPT_RESPECT:   ToggleOpt(g_s.idleRespectBusy); break;
    case ID_OPT_FADE:      ToggleOpt(g_s.fade); break;
    case ID_OPT_CURSOR:    ToggleOpt(g_s.hideCursor); break;
    case ID_OPT_CLOCK:     ToggleOpt(g_s.showClock); break;
    case ID_OPT_AWAKE:     ToggleOpt(g_s.keepAwake); break;
    case ID_OPT_POWEROFF:  ToggleOpt(g_s.powerOff); break;
    case ID_OPT_DDC:
        ToggleOpt(g_s.ddc);
        if (g_s.ddc)
            Balloon(L"DDC/CI standby", L"Experimental. Some monitors drop off the PC in standby or need their "
                                       L"power button to come back. Test it once before relying on it.");
        break;
    case ID_AUTOSTART:     SetAutostart(!AutostartEnabled()); break;
    case ID_EDIT_SETTINGS: OpenSettingsFile(); break;
    case ID_RELOAD:        LoadSettings(); ApplySettings(); break;
    case ID_ABOUT:         ShowAbout(); break;
    case ID_EXIT:          ExitApp(); break;
    case ID_SCREENS_WINDOW:  ShowScreensWindow(); break;
    case ID_SETTINGS_WINDOW: ShowSettingsWindow(); break;
    }
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED) {
        BuildTrayIcons();
        g_trayAdded = false;
        TrayAdd();
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ShowScreensWindow();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu({GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
            break;
        }
        return 0;
    case WM_HOTKEY:
        // The key that just woke a session can complete a hotkey chord; don't re-blank on it.
        if (Now() - g_lastWake < 800) return 0;
        switch (static_cast<int>(wp)) {
        case HK_BLANK:  ToggleBlankAll(static_cast<ULONGLONG>(g_s.graceMs)); break;
        case HK_OTHERS: ToggleBlankOthers(); break;
        case HK_CURSOR: RefreshMonitors(); ToggleMonitor(CursorMonitorIndex(), MODE_BLANK); break;
        case HK_DIM:    ToggleDimAll(); break;
        }
        return 0;
    case WM_APP_WAKE:
        WakeSession();
        return 0;
    case WM_APP_CLOSE_OVERLAY: {
        auto* target = reinterpret_cast<Overlay*>(lp);
        if (std::find(g_overlays.begin(), g_overlays.end(), target) != g_overlays.end()) {
            CloseOverlay(target);
            Sweep();
            UpdateState();
        }
        return 0;
    }
    case WM_APP_RUN_PENDING:
        RunCommand(g_pendingCmd);
        g_pendingCmd.clear();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;
    case WM_COPYDATA: {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
        if (cds && cds->dwData == kCopyDataTag && cds->lpData) {
            std::wstring cmd(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!cmd.empty() && cmd.back() == L'\0') cmd.pop_back();
            g_pendingCmd = cmd;
            PostMessageW(h, WM_APP_RUN_PENDING, 0, 0);  // don't run modal UI inside SendMessage
            return TRUE;
        }
        return FALSE;
    }
    case WM_TIMER:
        switch (wp) {
        case TIMER_ANIM:    AnimTick(); break;
        case TIMER_TOPMOST: ReassertTopmost(); break;
        case TIMER_IDLE:    IdleTick(); break;
        case TIMER_CLOCK:
            for (auto* o : g_overlays) {
                o->clockPos = {-1, -1};
                if (o->hwnd) InvalidateRect(o->hwnd, nullptr, FALSE);
            }
            break;
        case TIMER_RESTORE_WINDOWS_1:
            KillTimer(h, TIMER_RESTORE_WINDOWS_1);
            RestoreWindows();
            break;
        case TIMER_RESTORE_WINDOWS_2:
            KillTimer(h, TIMER_RESTORE_WINDOWS_2);
            if (g_detached.empty()) {
                RestoreWindows();
                g_savedWindows.clear();
            }
            break;
        case TIMER_DISPLAY:
            KillTimer(h, TIMER_DISPLAY);
            OnDisplayChanged();
            break;
        }
        return 0;
    case WM_DISPLAYCHANGE:
        SetTimer(h, TIMER_DISPLAY, 600, nullptr);  // let the monitor layout settle first
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) SetTimer(h, TIMER_DISPLAY, 600, nullptr);
        break;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) ExitApp();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void EnableDpiAwareness() {
    using SetCtx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

static bool SendToRunningInstance(const std::wstring& cmd) {
    HWND other = FindWindowW(kMainClass, nullptr);
    if (!other) return false;
    std::wstring payload = cmd.empty() ? L"screens" : cmd;
    COPYDATASTRUCT cds{};
    cds.dwData = kCopyDataTag;
    cds.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
    cds.lpData = const_cast<wchar_t*>(payload.c_str());
    DWORD_PTR result = 0;
    AllowSetForegroundWindow(ASFW_ANY);
    SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &result);
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    g_inst = inst;

    std::wstring cmd;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (!cmd.empty()) cmd += L' ';
            cmd += argv[i];
        }
        LocalFree(argv);
    }
    std::wstring verb = Lower(Trim(cmd));
    while (!verb.empty() && (verb[0] == L'-' || verb[0] == L'/')) verb.erase(0, 1);
    if (verb == L"?" || verb == L"h" || verb == L"help") {
        MessageBoxW(nullptr, kUsage, kAppName, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (verb == L"install-menu" || verb == L"uninstall-menu") {
        SetExplorerMenu(verb == L"install-menu");
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // "about" is shown for a bare second launch so the user sees it is already running.
        SendToRunningInstance(cmd);
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    if (verb == L"exit" || verb == L"quit" || verb == L"wake" || verb == L"on") {
        if (mutex) CloseHandle(mutex);
        return 0;  // nothing running, nothing to do
    }

    EnableDpiAwareness();
    std::srand(static_cast<unsigned>(GetTickCount()));
    InitIniPath();
    LoadSettings();
    {
        size_t slash = g_ini.find_last_of(L"\\/");
        g_logPath = (slash == std::wstring::npos ? std::wstring(L".") : g_ini.substr(0, slash)) + L"\\screenBlank.log";
    }
    Log(L"start " + ExePath());
    g_lastWake = Now();

    g_appIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!g_appIcon) g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hIcon = g_appIcon;
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW oc{sizeof(oc)};
    oc.style = CS_DBLCLKS;
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = inst;
    oc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    oc.lpszClassName = kOverlayClass;
    RegisterClassExW(&oc);

    WNDCLASSEXW ic{sizeof(ic)};
    ic.lpfnWndProc = IdentifyProc;
    ic.hInstance = inst;
    ic.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    ic.lpszClassName = kIdentifyClass;
    RegisterClassExW(&ic);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_HOTKEY_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW mc{sizeof(mc)};
    mc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    mc.lpfnWndProc = MapProc;
    mc.hInstance = inst;
    mc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    mc.lpszClassName = kMapClass;
    RegisterClassExW(&mc);

    // A real (hidden) top-level window, not message-only, so it receives TaskbarCreated
    // and FindWindow can locate it for second-instance commands.
    g_main = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                             nullptr);
    if (!g_main) return 1;

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_main, WM_TASKBARCREATED, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_main, WM_COPYDATA, MSGFLT_ALLOW, nullptr);

    DdcStart();
    RecoverDetachedOnStart();
    if (ExplorerMenuInstalled()) SetExplorerMenu(true);  // keep it pointing at this exe if it moved
    RefreshMonitors();
    BuildTrayIcons();
    TrayAdd();
    RegisterHotkeys(true);
    UpdateState();
    SetTimer(g_main, TIMER_IDLE, 5000, nullptr);

    if (!verb.empty()) {
        g_pendingCmd = cmd;
        PostMessageW(g_main, WM_APP_RUN_PENDING, 0, 0);
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (g_hScreens && IsDialogMessageW(g_hScreens, &m)) continue;
        if (g_hSettings && IsDialogMessageW(g_hSettings, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    DdcStop();  // waits for pending brightness/power restores
    for (HICON i : g_trayIcons)
        if (i) DestroyIcon(i);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
