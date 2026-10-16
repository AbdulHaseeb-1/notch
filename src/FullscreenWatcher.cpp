#include "FullscreenWatcher.h"

#include <QString>
#include <QStringList>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace {
constexpr int kCheckMs = 1000;
}

FullscreenWatcher::FullscreenWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kCheckMs);
    connect(&m_timer, &QTimer::timeout, this, &FullscreenWatcher::check);
    m_timer.start();
    check();
}

void FullscreenWatcher::check()
{
    bool active = false;

#ifdef Q_OS_WIN
    const HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        bool candidate = pid != 0 && pid != GetCurrentProcessId() && IsWindowVisible(hwnd)
                      && !IsIconic(hwnd);

        if (candidate) {
            // Cloaked windows belong to another virtual desktop.
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
                && cloaked)
                candidate = false;
        }

        if (candidate) {
            // The desktop and shell surfaces can report monitor-sized rects;
            // they are not "full-screen apps" for our purposes.
            wchar_t cls[64] = {};
            GetClassNameW(hwnd, cls, 64);
            static const QStringList shellClasses = {
                QStringLiteral("Progman"),
                QStringLiteral("WorkerW"),
                QStringLiteral("Shell_TrayWnd"),
                QStringLiteral("Shell_SecondaryTrayWnd"),
                QStringLiteral("MultitaskingViewFrame"),
                QStringLiteral("XamlExplorerHostIslandWindow"),
                QStringLiteral("ForegroundStaging"),
            };
            const QString className = QString::fromWCharArray(cls);
            if (shellClasses.contains(className))
                candidate = false;
        }

        if (candidate) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            RECT rect{};
            // The standard frame extends past the monitor edge behind the task
            // bar; the DWM's extended frame bounds are the window as drawn, so
            // a maximised window no longer pretends to be full-screen. Fall
            // back to GetWindowRect when the attribute is not available.
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                             sizeof(rect)))) {
                GetWindowRect(hwnd, &rect);
            }
            if (GetMonitorInfoW(monitor, &info)) {
                constexpr LONG tolerance = 2;
                active = rect.left <= info.rcMonitor.left + tolerance
                      && rect.top <= info.rcMonitor.top + tolerance
                      && rect.right >= info.rcMonitor.right - tolerance
                      && rect.bottom >= info.rcMonitor.bottom - tolerance;
            }
        }
    }
#endif

    if (active != m_active) {
        m_active = active;
        emit activeChanged();
    }
}
