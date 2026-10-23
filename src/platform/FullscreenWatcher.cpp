#include "FullscreenWatcher.h"

#include "WindowMonitor.h"

#include <QRect>
#include <QString>
#include <QStringList>
#include <QWindow>

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

#ifdef Q_OS_WIN

// A WinEvent hook has no per-instance context, so keep both the handle and the
// live object in file scope. Windows delivers foreground changes on the GUI
// thread, so the callback can call into the object directly.
FullscreenWatcher *s_liveWatcher = nullptr;
HWINEVENTHOOK s_hookHandle = nullptr;

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    // Relisting the windows also re-checks the full-screen state.
    if (s_liveWatcher)
        s_liveWatcher->checkZone();
}

// Whether a window belongs to the shell (desktop, task bar ...), not to an app.
bool isShellClass(const QString &className)
{
    static const QStringList shellClasses = {
        QStringLiteral("Progman"),
        QStringLiteral("WorkerW"),
        QStringLiteral("Shell_TrayWnd"),
        QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("MultitaskingViewFrame"),
        QStringLiteral("XamlExplorerHostIslandWindow"),
        QStringLiteral("ForegroundStaging"),
        QStringLiteral("Windows.UI.Core.CoreWindow"),
        QStringLiteral("NotifyIconOverflowWindow"),
    };
    return shellClasses.contains(className);
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

FullscreenWatcher::FullscreenWatcher(WindowMonitor *windows, QObject *parent)
    : QObject(parent)
    , m_windows(windows)
{
#ifdef Q_OS_WIN
    s_liveWatcher = this;
    startForegroundHook();
#endif
    // The listing ticks once a second and the foreground hook catches the
    // changes in between, so no timer of our own is needed.
    connect(m_windows, &WindowMonitor::refreshed, this, &FullscreenWatcher::onWindowsRefreshed);
    check();
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

void FullscreenWatcher::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    emit windowChanged();
    checkZone();
}

void FullscreenWatcher::setZone(const QRectF &zone)
{
    if (m_zone == zone)
        return;
    m_zone = zone;
    emit zoneChanged();
    checkZone();
}

void FullscreenWatcher::checkZone()
{
    // Relisting the windows is what answers the question; onWindowsRefreshed()
    // reads the answer from the new listing.
    m_windows->refresh();
}

void FullscreenWatcher::onWindowsRefreshed()
{
    check();
    updateZone();
}

void FullscreenWatcher::updateZone()
{
    bool occupied = false;
    QString by;

#ifdef Q_OS_WIN
    if (m_window && !m_zone.isEmpty() && m_window->width() > 0) {
        const HWND self = reinterpret_cast<HWND>(m_window->winId());
        RECT wr{};
        if (self && GetWindowRect(self, &wr)) {
            // The zone is in the window's own (device-independent) coordinates;
            // the window's rectangle on screen tells where and how big that is.
            const double scale = double(wr.right - wr.left) / double(m_window->width());
            const int left = wr.left + int(m_zone.left() * scale);
            const int top = wr.top + int(m_zone.top() * scale);
            const int right = wr.left + int(m_zone.right() * scale);
            const int bottom = wr.top + int(m_zone.bottom() * scale);
            const QRect zone(left, top, right - left, bottom - top);

            for (const WindowMonitor::Window &window : m_windows->windows()) {
                // Noch's own windows, minimised ones, and dialogs and popups (they
                // belong to a window that is already looked at).
                if (window.self || window.minimized || window.owned)
                    continue;
                // Tool windows, windows that never take focus (overlays, game bars)
                // and click-through ones are not "an app that is open".
                if (window.toolWindow || window.noActivate || window.transparent)
                    continue;
                if (window.nearlyInvisible || window.cloaked || isShellClass(window.className))
                    continue;
                if (window.rect.width() < 120 || window.rect.height() < 60)
                    continue;
                if (!window.rect.intersects(zone))
                    continue;

                by = QStringLiteral("%1 [%2]").arg(window.title, window.className);
                occupied = true;
                break;   // topmost first, so the first hit is the one on top
            }
        }
    }
#endif

    if (occupied != m_occupied || by != m_occupiedBy) {
        m_occupied = occupied;
        m_occupiedBy = by;
        emit occupiedChanged();
    }
}
