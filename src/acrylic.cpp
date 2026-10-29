#include "acrylic.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Acrylic {

#ifdef Q_OS_WIN
namespace {

// Undocumented but widely-used composition attribute API (Windows 10+).
enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attribute;
    PVOID Data;
    SIZE_T SizeOfData;
};

enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *);

// DWMWA_SYSTEMBACKDROP_TYPE (Win11 22H2+)
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// Backdrop types
enum DWM_SYSTEMBACKDROP_TYPE_LOCAL {
    DWMSBT_AUTO = 0,
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,      // Mica
    DWMSBT_TRANSIENTWINDOW = 3, // Acrylic
    DWMSBT_TABBEDWINDOW = 4
};

int windowsBuildNumber()
{
    typedef LONG(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(
            reinterpret_cast<void *>(GetProcAddress(hMod, "RtlGetVersion")));
        if (fn) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                return static_cast<int>(info.dwBuildNumber);
            }
        }
    }
    return 0;
}

} // namespace
#endif // Q_OS_WIN

bool enable(QWidget *window, const QColor &tint, bool darkMode)
{
#ifdef Q_OS_WIN
    if (!window) return false;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return false;

    setDarkTitleBar(window, darkMode);
    enableRoundedCorners(window);

    const int build = windowsBuildNumber();

    if (build >= 22000) {
        int backdrop = DWMSBT_TRANSIENTWINDOW;
        HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        if (SUCCEEDED(hr)) return true;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setAttr = reinterpret_cast<SetWindowCompositionAttributeFn>(
            reinterpret_cast<void *>(GetProcAddress(user32, "SetWindowCompositionAttribute")));
        if (setAttr) {
            ACCENT_POLICY policy{};
            policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            policy.AccentFlags = 2;
            const DWORD a = 0xC0;
            const DWORD r = static_cast<DWORD>(tint.red());
            const DWORD g = static_cast<DWORD>(tint.green());
            const DWORD b = static_cast<DWORD>(tint.blue());
            policy.GradientColor = (a << 24) | (b << 16) | (g << 8) | r;
            WINDOWCOMPOSITIONATTRIBDATA data{};
            data.Attribute = WCA_ACCENT_POLICY;
            data.Data = &policy;
            data.SizeOfData = sizeof(policy);
            return setAttr(hwnd, &data) != FALSE;
        }
    }
    return false;
#else
    Q_UNUSED(window);
    Q_UNUSED(tint);
    Q_UNUSED(darkMode);
    return false;
#endif
}

void enableRoundedCorners(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window) return;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;
    int pref = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
#else
    Q_UNUSED(window);
#endif
}

void setDarkTitleBar(QWidget *window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window) return;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

} // namespace Acrylic
