#include "MenuIcons.h"

#include "DarkMode.h"

#include <algorithm>
#include <cstring>

namespace MenuIcons {
namespace {

// Creates the first of the icon fonts that's installed, at `size` pixels.
HFONT CreateIconFont(int size) {
    HDC screen = GetDC(nullptr);
    HFONT found = nullptr;
    for (const wchar_t* face : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        if (!font) continue;
        // Windows substitutes another font for a missing one; check what we got.
        wchar_t actual[LF_FACESIZE] = {};
        HGDIOBJ old = SelectObject(screen, font);
        GetTextFaceW(screen, LF_FACESIZE, actual);
        SelectObject(screen, old);
        if (_wcsicmp(actual, face) == 0) {
            found = font;
            break;
        }
        DeleteObject(font);
    }
    ReleaseDC(nullptr, screen);
    return found;
}

}  // namespace

Set::Set(HWND window) {
    UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    size_ = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    font_ = CreateIconFont(size_);
}

Set::~Set() {
    for (HBITMAP bitmap : bitmaps_) DeleteObject(bitmap);
    if (font_) DeleteObject(font_);
}

void Set::Add(HMENU menu, UINT position, Glyph glyph, bool enabled, bool red) {
    if (!font_) return;  // no icon font: plain menu
    const bool dark = DarkMode::IsDark();
    COLORREF colour = red ? (dark ? RGB(0xFF, 0x99, 0xA4) : RGB(0xC4, 0x2B, 0x1C))
                          : (dark ? RGB(0xFF, 0xFF, 0xFF) : GetSysColor(COLOR_MENUTEXT));
    // Menus don't grey out an item's bitmap, so a disabled one is drawn faded.
    HBITMAP bitmap = Render(glyph, colour, enabled ? 255 : (dark ? 90 : 110));
    if (!bitmap) return;
    bitmaps_.push_back(bitmap);
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_BITMAP;
    item.hbmpItem = bitmap;
    SetMenuItemInfoW(menu, position, TRUE, &item);
}

// Draws the glyph white-on-black, takes the brightness as coverage and turns
// it into a premultiplied-alpha bitmap of `colour`, which menus blend properly.
HBITMAP Set::Render(Glyph glyph, COLORREF colour, BYTE opacity) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size_;
    info.bmiHeader.biHeight = size_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto* pixels = static_cast<DWORD*>(bits);
    const size_t count = static_cast<size_t>(size_) * size_;
    std::memset(pixels, 0, count * sizeof(DWORD));

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT bounds{0, 0, size_, size_};
    wchar_t text[2] = {static_cast<wchar_t>(glyph), 0};
    DrawTextW(dc, text, 1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);

    const DWORD r = GetRValue(colour), g = GetGValue(colour), b = GetBValue(colour);
    for (size_t i = 0; i < count; ++i) {
        DWORD p = pixels[i];
        DWORD coverage = std::max({p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF});
        DWORD a = coverage * opacity / 255;
        pixels[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
    }
    return bitmap;
}

}  // namespace MenuIcons
