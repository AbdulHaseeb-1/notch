#include "FullscreenWatcher.h"

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

namespace {
constexpr int kCheckMs = 2000;
constexpr int kZoneCheckMs = 1000;
}

#ifdef Q_OS_WIN

// A WinEvent hook has no per-instance context, so keep both the handle and the
// live object in file scope. Windows delivers foreground changes on the GUI
// thread, so the callback can call into the object directly.
FullscreenWatcher *s_liveWatcher = nullptr;
HWINEVENTHOOK s_hookHandle = nullptr;

void CALLBACK foregroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    if (s_liveWatcher) {
        s_liveWatcher->check();
        s_liveWatcher->checkZone();
    }
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

struct ZoneScan {
    RECT zone{};
    DWORD ownPid = 0;
    bool hit = false;
    QString by;
};

BOOL CALLBACK scanWindow(HWND hwnd, LPARAM lParam)
{
    auto *scan = reinterpret_cast<ZoneScan *>(lParam);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == scan->ownPid)
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER))
        return TRUE;   // dialogs and popups belong to a window we already look at
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // Tool windows, windows that never take focus (overlays, game bars) and
    // click-through ones are not "an app that is open".
    if (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT))
        return TRUE;
    if (ex & WS_EX_LAYERED) {
        COLORREF key = 0;
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 24)
            return TRUE;   // practically invisible
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;       // on another virtual desktop
    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, 96);
    const QString className = QString::fromWCharArray(cls);
    if (isShellClass(className))
        return TRUE;

    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    if (r.right - r.left < 120 || r.bottom - r.top < 60)
        return TRUE;
    RECT overlap{};
    if (!IntersectRect(&overlap, &r, &scan->zone))
        return TRUE;

    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, 128);
    scan->by = QStringLiteral("%1 [%2]").arg(QString::fromWCharArray(title), className);
    scan->hit = true;
    return FALSE;
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
    m_zoneTimer.setInterval(kZoneCheckMs);
    connect(&m_zoneTimer, &QTimer::timeout, this, &FullscreenWatcher::checkZone);
    m_zoneTimer.start();
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
            ZoneScan scan;
            scan.ownPid = GetCurrentProcessId();
            scan.zone.left = wr.left + LONG(m_zone.left() * scale);
            scan.zone.top = wr.top + LONG(m_zone.top() * scale);
            scan.zone.right = wr.left + LONG(m_zone.right() * scale);
            scan.zone.bottom = wr.top + LONG(m_zone.bottom() * scale);
            EnumWindows(&scanWindow, reinterpret_cast<LPARAM>(&scan));
            occupied = scan.hit;
            by = scan.by;
        }
    }
#endif

    if (occupied != m_occupied || by != m_occupiedBy) {
        m_occupied = occupied;
        m_occupiedBy = by;
        emit occupiedChanged();
    }
}
