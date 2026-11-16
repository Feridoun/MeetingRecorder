#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Message boxes as Windows task dialogs: a bold heading, plain body text and
// buttons that say what they do ("Upload", "Delete") instead of Yes/No.

enum class DialogIcon { kNone, kInfo, kWarning, kError };

struct DialogButton {
    int id;                // returned when chosen; must not be IDCANCEL
    const wchar_t* label;  // a command link's first line may be followed by "\n" and a note
};

struct DialogOptions {
    DialogIcon icon = DialogIcon::kNone;
    std::wstring heading;
    std::wstring body;
    std::vector<DialogButton> buttons;            // empty: just OK
    int defaultButton = IDCANCEL;                 // with no buttons, OK is the default
    bool commandLinks = false;                    // big stacked buttons, for choices that need a note
};

// Returns the chosen button's id, IDOK for the plain OK, or IDCANCEL if the
// user cancelled (Esc, Cancel or the close box). With buttons, a Cancel
// button is always added.
int ShowDialog(HWND owner, const DialogOptions& options);

// An OK-only notice.
void Tell(HWND owner, DialogIcon icon, const std::wstring& heading, const std::wstring& body = {});

// True if the user chose `action`; Cancel is the default.
bool Confirm(HWND owner, DialogIcon icon, const std::wstring& heading, const std::wstring& body,
             const wchar_t* action);
