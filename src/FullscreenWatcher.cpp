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
constexpr int kCheckMs = 2000;
}

#ifdef Q_OS_WIN

// A WinEvent hook has no per-instance context, so keep both the handle and the
// live object in file scope. Windows delivers foreground changes on the GUI
// thread, so the callback can call into the object directly.
FullscreenWatcher *s_liveWatcher = nullptr;
HWINEVENTHOOK s_hookHandle = nullptr;

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    if (s_liveWatcher)
        s_liveWatcher->check();
}

void startForegroundHook()
{
    s_hookHandle = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                   &foregroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
}

void stopForegroundHook()
{
    if (s_hookHandle) {
        UnhookWinEvent(s_hookHandle);
        s_hookHandle = nullptr;
    }
}

#endif

FullscreenWatcher::FullscreenWatcher(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    s_liveWatcher = this;
    startForegroundHook();
#endif
    m_timer.setInterval(kCheckMs);
    connect(this, &FullscreenWatcher::activeChanged, this, [this] {
        // The hook catches every transition already; the timer only has to
        // keep verifying while the guard is actually up (borderline
        // full-screen apps, a window that swapped monitors without a
        // foreground event, etc.), so the peaceful desktop pays nothing.
        if (m_active)
            m_timer.start();
        else
            m_timer.stop();
    });
    check();
    if (m_active)
        m_timer.start();
}

FullscreenWatcher::~FullscreenWatcher()
{
#ifdef Q_OS_WIN
    s_liveWatcher = nullptr;
    stopForegroundHook();
#endif
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
