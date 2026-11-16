#pragma once

#include <windows.h>

#include <vector>

// Icons for menu items, drawn from the Windows icon font (Segoe Fluent Icons
// on Windows 11, Segoe MDL2 Assets on Windows 10) in the menu's text colour.
namespace MenuIcons {

// Glyphs shared by both fonts.
enum Glyph : wchar_t {
    kRecord = 0xE7C8,
    kStop = 0xE71A,
    kPause = 0xE769,
    kPlay = 0xE768,
    kFlag = 0xE7C1,
    kMicrophone = 0xE720,
    kLock = 0xE72E,
    kTag = 0xE8EC,
    kComputer = 0xE7F4,  // a monitor
    kUpload = 0xE898,
    kRefresh = 0xE72C,
    kOpenInNew = 0xE8A7,
    kHistory = 0xE81C,
    kFolderOpen = 0xE838,
    kSettings = 0xE713,
    kPower = 0xE7E8,
};

// Owns the bitmaps for one showing of a menu; they're freed with it, after
// the menu is destroyed.
class Set {
public:
    explicit Set(HWND window);
    ~Set();
    Set(const Set&) = delete;
    Set& operator=(const Set&) = delete;

    // Gives the item at `position` in `menu` an icon. `red` is for recording.
    void Add(HMENU menu, UINT position, Glyph glyph, bool enabled = true, bool red = false);

private:
    HBITMAP Render(Glyph glyph, COLORREF colour, BYTE opacity);

    int size_ = 16;
    HFONT font_ = nullptr;
    std::vector<HBITMAP> bitmaps_;
};

}  // namespace MenuIcons
