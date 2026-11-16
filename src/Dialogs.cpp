#include "Dialogs.h"

#include <commctrl.h>

#include <vector>

namespace {

constexpr wchar_t kAppName[] = L"MeetingRecorder";

PCWSTR StockIcon(DialogIcon icon) {
    switch (icon) {
        case DialogIcon::kInfo: return TD_INFORMATION_ICON;
        case DialogIcon::kWarning: return TD_WARNING_ICON;
        case DialogIcon::kError: return TD_ERROR_ICON;
        default: return nullptr;
    }
}

// A tray app's dialog can open while another app has the focus.
HRESULT CALLBACK Callback(HWND dialog, UINT notification, WPARAM, LPARAM, LONG_PTR) {
    if (notification == TDN_CREATED) SetForegroundWindow(dialog);
    return S_OK;
}

// Used only if the task dialog can't be shown at all.
int FallbackMessageBox(HWND owner, const DialogOptions& options) {
    std::wstring text = options.heading;
    if (!options.body.empty()) text += L"\n\n" + options.body;
    UINT icon = options.icon == DialogIcon::kInfo      ? MB_ICONINFORMATION
                : options.icon == DialogIcon::kWarning ? MB_ICONWARNING
                : options.icon == DialogIcon::kError   ? MB_ICONERROR
                                                       : 0;
    if (options.buttons.empty()) {
        MessageBoxW(owner, text.c_str(), kAppName, MB_OK | icon | MB_SETFOREGROUND);
        return IDOK;
    }
    // OK picks the first button; anything else counts as Cancel.
    std::wstring label = options.buttons.front().label;
    text += L"\n\nOK: " + label.substr(0, label.find(L'\n')) + L".";
    return MessageBoxW(owner, text.c_str(), kAppName, MB_OKCANCEL | icon | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDOK
               ? options.buttons.front().id
               : IDCANCEL;
}

}  // namespace

int ShowDialog(HWND owner, const DialogOptions& options) {
    std::vector<TASKDIALOG_BUTTON> buttons;
    for (const auto& button : options.buttons) buttons.push_back({button.id, button.label});

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    if (owner && IsWindowVisible(owner)) config.dwFlags |= TDF_POSITION_RELATIVE_TO_WINDOW;
    if (options.commandLinks) config.dwFlags |= TDF_USE_COMMAND_LINKS;
    config.pszWindowTitle = kAppName;
    config.pszMainIcon = StockIcon(options.icon);
    config.pszMainInstruction = options.heading.c_str();
    config.pszContent = options.body.empty() ? nullptr : options.body.c_str();
    config.pfCallback = Callback;
    if (buttons.empty()) {
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        config.nDefaultButton = IDOK;
    } else {
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pButtons = buttons.data();
        config.cButtons = static_cast<UINT>(buttons.size());
        config.nDefaultButton = options.defaultButton;
    }

    int chosen = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &chosen, nullptr, nullptr))) return FallbackMessageBox(owner, options);
    return chosen;
}

void Tell(HWND owner, DialogIcon icon, const std::wstring& heading, const std::wstring& body) {
    DialogOptions options;
    options.icon = icon;
    options.heading = heading;
    options.body = body;
    ShowDialog(owner, options);
}

bool Confirm(HWND owner, DialogIcon icon, const std::wstring& heading, const std::wstring& body,
             const wchar_t* action) {
    constexpr int kAction = 100;
    DialogOptions options;
    options.icon = icon;
    options.heading = heading;
    options.body = body;
    options.buttons = {{kAction, action}};
    return ShowDialog(owner, options) == kAction;
}
