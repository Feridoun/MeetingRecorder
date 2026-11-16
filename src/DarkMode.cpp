#include "DarkMode.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <string>

namespace DarkMode {
namespace {

// Windows 11's dark palette.
constexpr COLORREF kBackground = RGB(0x20, 0x20, 0x20);
constexpr COLORREF kControl = RGB(0x2B, 0x2B, 0x2B);
constexpr COLORREF kText = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kDisabledText = RGB(0x80, 0x80, 0x80);

// uxtheme.dll exports by ordinal only.
enum class PreferredAppMode { kDefault, kAllowDark, kForceDark, kForceLight };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);  // #135, 1903+
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);                  // #133
using FlushMenuThemesFn = void(WINAPI*)();                                   // #136
using RefreshImmersiveColorPolicyStateFn = void(WINAPI*)();                  // #104

constexpr DWORD kFirstSupportedBuild = 18362;  // Windows 10 1903
// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from build 19041 (in the SDK); it was 19 before.
constexpr DWORD kOldDarkTitleBarAttribute = 19;

struct State {
    bool supported = false;
    bool dark = false;
    DWORD build = 0;
    AllowDarkModeForWindowFn allowForWindow = nullptr;
    FlushMenuThemesFn flushMenuThemes = nullptr;
    RefreshImmersiveColorPolicyStateFn refreshPolicy = nullptr;
    HBRUSH background = nullptr;
    HBRUSH control = nullptr;
};
State state;

DWORD WindowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto getVersion = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW info{sizeof(info)};
    return getVersion && getVersion(&info) == 0 ? info.dwBuildNumber : 0;
}

bool AppsUseDarkTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

bool HighContrast() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

// Re-reads the setting; true if it changed.
bool Update() {
    bool dark = state.supported && AppsUseDarkTheme() && !HighContrast();
    if (state.supported) {
        state.refreshPolicy();
        state.flushMenuThemes();
    }
    bool changed = dark != state.dark;
    state.dark = dark;
    return changed;
}

void AllowWindow(HWND window, bool dark) {
    if (state.allowForWindow) state.allowForWindow(window, dark);
}

std::wstring ClassOf(HWND window) {
    wchar_t name[64] = {};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return name;
}

bool IsCheckBox(HWND window) {
    if (_wcsicmp(ClassOf(window).c_str(), WC_BUTTONW) != 0) return false;
    LONG type = GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK;
    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
}

// The header is the list view's child, so its custom-draw notifications go to
// the list view rather than to our window procedure.
LRESULT CALLBACK ListViewSubclass(HWND list, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_NOTIFY && state.dark) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == ListView_GetHeader(list) && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(draw->hdc, kText);
                return CDRF_DODEFAULT;
            }
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(list, ListViewSubclass, 1);
    return DefSubclassProc(list, message, wParam, lParam);
}

BOOL CALLBACK ThemeChild(HWND child, LPARAM) {
    const bool dark = state.dark;
    const std::wstring name = ClassOf(child);
    AllowWindow(child, dark);  // also darkens its scroll bars
    if (_wcsicmp(name.c_str(), WC_EDITW) == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_CFD" : nullptr, nullptr);
    } else if (_wcsicmp(name.c_str(), WC_COMBOBOXW) == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_CFD" : nullptr, nullptr);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(child, &info) && info.hwndList) {
            AllowWindow(info.hwndList, dark);
            SetWindowTheme(info.hwndList, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        }
    } else if (_wcsicmp(name.c_str(), WC_BUTTONW) == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    } else if (_wcsicmp(name.c_str(), WC_LISTVIEWW) == 0) {
        // Explorer's item highlights in both modes.
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        ListView_SetBkColor(child, dark ? kBackground : GetSysColor(COLOR_WINDOW));
        ListView_SetTextBkColor(child, dark ? kBackground : GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor(child, dark ? kText : GetSysColor(COLOR_WINDOWTEXT));
        SetWindowSubclass(child, ListViewSubclass, 1, 0);
    } else if (_wcsicmp(name.c_str(), WC_HEADERW) == 0) {
        SetWindowTheme(child, dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
    }
    return TRUE;
}

// A themed checkbox ignores the text colour its parent asks for, so in dark
// mode we draw it: the theme's own box, then the label in light text.
void DrawCheckBox(const NMCUSTOMDRAW& draw) {
    HWND button = draw.hdr.hwndFrom;
    HDC dc = draw.hdc;
    RECT bounds = draw.rc;
    FillRect(dc, &bounds, state.background);

    const UINT dpi = GetDpiForWindow(button);
    const bool checked = SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool disabled = draw.uItemState & CDIS_DISABLED;
    int part = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
    if (disabled) {
        part += 3;
    } else if (draw.uItemState & CDIS_SELECTED) {
        part += 2;
    } else if (draw.uItemState & CDIS_HOT) {
        part += 1;
    }

    SIZE box{MulDiv(13, dpi, 96), MulDiv(13, dpi, 96)};
    HTHEME theme = OpenThemeData(button, L"Button");
    if (theme) GetThemePartSize(theme, dc, BP_CHECKBOX, part, nullptr, TS_DRAW, &box);
    RECT boxRect{bounds.left, bounds.top + (bounds.bottom - bounds.top - box.cy) / 2, 0, 0};
    boxRect.right = boxRect.left + box.cx;
    boxRect.bottom = boxRect.top + box.cy;
    if (theme) {
        DrawThemeBackground(theme, dc, BP_CHECKBOX, part, &boxRect, nullptr);
        CloseThemeData(theme);
    } else {
        DrawFrameControl(dc, &boxRect, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));
    }

    std::wstring text(GetWindowTextLengthW(button) + 1, L'\0');
    text.resize(GetWindowTextW(button, text.data(), static_cast<int>(text.size())));
    HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? kDisabledText : kText);
    RECT textRect = bounds;
    textRect.left = boxRect.right + MulDiv(5, dpi, 96);
    const bool cues = draw.uItemState & CDIS_SHOWKEYBOARDCUES;
    const UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT | (cues ? 0 : DT_HIDEPREFIX);
    DrawTextW(dc, text.c_str(), -1, &textRect, flags);
    if ((draw.uItemState & CDIS_FOCUS) && cues) {
        RECT focus = textRect;
        DrawTextW(dc, text.c_str(), -1, &focus, flags | DT_CALCRECT);
        const int height = focus.bottom - focus.top;
        focus.top = textRect.top + (textRect.bottom - textRect.top - height) / 2;
        focus.bottom = focus.top + height;
        InflateRect(&focus, 1, 1);
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, oldFont);
}

}  // namespace

void Init() {
    state.build = WindowsBuild();
    state.background = CreateSolidBrush(kBackground);
    state.control = CreateSolidBrush(kControl);
    if (state.build >= kFirstSupportedBuild) {
        HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (uxtheme) {
            auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
            state.allowForWindow =
                reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
            state.flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
            state.refreshPolicy =
                reinterpret_cast<RefreshImmersiveColorPolicyStateFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(104)));
            if (setMode && state.allowForWindow && state.flushMenuThemes && state.refreshPolicy) {
                setMode(PreferredAppMode::kAllowDark);
                state.supported = true;
            } else {
                state.allowForWindow = nullptr;
            }
        }
    }
    Update();
}

bool IsDark() {
    return state.dark;
}

void AllowForMenus(HWND window) {
    AllowWindow(window, true);
    if (state.supported) state.flushMenuThemes();
}

bool OnSettingChange(WPARAM wParam, LPARAM lParam) {
    const auto* area = reinterpret_cast<const wchar_t*>(lParam);
    bool colours = area && CompareStringOrdinal(area, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL;
    if (!colours && wParam != SPI_SETHIGHCONTRAST) return false;
    return Update();
}

void Apply(HWND window) {
    AllowWindow(window, state.dark);
    if (state.supported) {
        BOOL dark = state.dark;
        DWORD attribute = state.build >= 19041 ? DWMWA_USE_IMMERSIVE_DARK_MODE : kOldDarkTitleBarAttribute;
        DwmSetWindowAttribute(window, attribute, &dark, sizeof(dark));
    }
    EnumChildWindows(window, ThemeChild, 0);
}

bool HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (message == WM_SETTINGCHANGE) {
        // Each window re-reads it; only the first sees a change, so apply regardless.
        OnSettingChange(wParam, lParam);
        Apply(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        return false;  // let the window see it too
    }
    if (!state.dark) return false;
    switch (message) {
        case WM_CTLCOLORDLG:
            result = reinterpret_cast<LRESULT>(state.background);
            return true;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lParam)) ? kText : kDisabledText);
            SetBkColor(dc, kBackground);
            result = reinterpret_cast<LRESULT>(state.background);
            return true;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kControl);
            result = reinterpret_cast<LRESULT>(state.control);
            return true;
        }
        case WM_ERASEBKGND: {
            RECT client;
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, state.background);
            result = 1;
            return true;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->code != NM_CUSTOMDRAW || !IsCheckBox(header->hwndFrom)) return false;
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                DrawCheckBox(*draw);
                result = CDRF_SKIPDEFAULT;
            } else {
                result = CDRF_DODEFAULT;
            }
            return true;
        }
    }
    return false;
}

bool HandleDialogMessage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam, INT_PTR& result) {
    LRESULT handled = 0;
    if (!HandleMessage(dialog, message, wParam, lParam, handled)) return false;
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        result = static_cast<INT_PTR>(handled);
    } else {
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, handled);
        result = TRUE;
    }
    return true;
}

}  // namespace DarkMode
