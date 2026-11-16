#pragma once

#include <windows.h>

// Follows the Windows "app mode" (Settings > Personalisation > Colours) for the
// tray menu, title bars and the app's windows. Uses the dark themes Windows
// itself uses (DarkMode_Explorer, DarkMode_CFD), switched on through uxtheme
// exports that aren't documented but have been stable since Windows 10 1903.
// On older builds, or with high contrast on, everything stays light.
namespace DarkMode {

// Once at startup, before any window is created.
void Init();

// True if the app's windows and menus are dark right now.
bool IsDark();

// Lets the hidden tray window's popup menus turn dark.
void AllowForMenus(HWND window);

// Re-reads the light/dark choice if a WM_SETTINGCHANGE says it may have
// changed. Returns true if it did.
bool OnSettingChange(WPARAM wParam, LPARAM lParam);

// Themes a top-level window's title bar and all its child controls. Call once
// the controls exist, and again when the theme changes (HandleMessage does).
void Apply(HWND window);

// Call first in a window or dialog procedure. Handles theme changes, control
// colours and drawing dark checkboxes; returns true with `result` set if it
// handled the message. In a dialog procedure, return `result` directly for
// WM_CTLCOLOR* messages and set it as DWLP_MSGRESULT for the rest.
bool HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);

// HandleMessage wrapped for a DialogProc.
bool HandleDialogMessage(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam, INT_PTR& result);

}  // namespace DarkMode
