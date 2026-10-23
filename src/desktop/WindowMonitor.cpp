#include "WindowMonitor.h"

#include <QFileInfo>
#include <QHash>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace {

constexpr int kListMs = 1000;

#ifdef Q_OS_WIN
QHash<DWORD, QString> s_pidCache;

// The program a process runs from. Opening a process is the slow part, so each
// answer is kept by process id (the cache is cleared when it grows large).
QString cachedProgramPath(DWORD pid)
{
    auto it = s_pidCache.constFind(pid);
    if (it != s_pidCache.constEnd())
        return it.value();

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[4096];
    DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, static_cast<int>(size));
    CloseHandle(process);

    if (!path.isEmpty()) {
        if (s_pidCache.size() > 512)
            s_pidCache.clear();
        s_pidCache.insert(pid, path);
    }
    return path;
}

// Every visible top-level window, with the facts each consumer needs. The
// program path is only looked up for windows that could be a program tile.
BOOL CALLBACK listWindow(HWND hwnd, LPARAM lParam)
{
    auto *out = reinterpret_cast<QList<WindowMonitor::Window> *>(lParam);
    if (!IsWindowVisible(hwnd))
        return TRUE;

    WindowMonitor::Window window;
    window.id = double(reinterpret_cast<quintptr>(hwnd));
    window.minimized = IsIconic(hwnd) != FALSE;
    window.owned = GetWindow(hwnd, GW_OWNER) != nullptr;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    window.appWindow = (style & WS_EX_APPWINDOW) != 0;
    window.toolWindow = (style & WS_EX_TOOLWINDOW) != 0;
    window.noActivate = (style & WS_EX_NOACTIVATE) != 0;
    window.transparent = (style & WS_EX_TRANSPARENT) != 0;
    if (style & WS_EX_LAYERED) {
        COLORREF key = 0;
        BYTE alpha = 255;
        DWORD flags = 0;
        window.nearlyInvisible = GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags)
                              && (flags & LWA_ALPHA) && alpha < 24;
    }

    // Cloaked windows belong to another virtual desktop.
    DWORD cloaked = 0;
    window.cloaked = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
                  && cloaked != 0;

    wchar_t cls[96] = {};
    GetClassNameW(hwnd, cls, 96);
    window.className = QString::fromWCharArray(cls);

    constexpr int kTitleChars = 512;
    wchar_t title[kTitleChars];
    const int length = GetWindowTextW(hwnd, title, kTitleChars);
    if (length > 0)
        window.title = QString::fromWCharArray(title, length);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    window.pid = pid;
    window.self = pid == GetCurrentProcessId();

    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    window.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);

    // A program tile is an app window, or a window that is neither a tool window
    // nor an owned dialog, and is not cloaked, titled, Noch's own or the desktop's.
    const bool tileCandidate = !window.self && pid != 0 && !window.cloaked && !window.title.isEmpty()
                            && (window.appWindow || (!window.toolWindow && !window.owned));
    if (tileCandidate) {
        window.path = cachedProgramPath(pid);
        window.exe = QFileInfo(window.path).fileName().toLower();
        window.program = !window.path.isEmpty() && window.title != QLatin1String("Program Manager");
    }
    out->append(window);
    return TRUE;
}
#endif

QList<WindowMonitor::Window> listWindows()
{
    QList<WindowMonitor::Window> windows;
#ifdef Q_OS_WIN
    EnumWindows(listWindow, reinterpret_cast<LPARAM>(&windows));
#else
    // Placeholder windows so the layout can be exercised off Windows. The path
    // is the name, so the icon falls back to the monogram.
    const auto placeholder = [&windows](double id, const QString &title, const QString &exe,
                                        const QString &name) {
        WindowMonitor::Window window;
        window.id = id;
        window.title = title;
        window.exe = exe;
        window.path = name;
        window.program = true;
        windows.append(window);
    };
    placeholder(1, QStringLiteral("Claude - New chat"), QStringLiteral("claude"), QStringLiteral("Claude"));
    placeholder(2, QStringLiteral("main.cpp - notch"), QStringLiteral("cursor"), QStringLiteral("Cursor"));
    placeholder(3, QStringLiteral("README.md - notch"), QStringLiteral("cursor"), QStringLiteral("Cursor"));
    placeholder(4, QStringLiteral("bash - notch"), QStringLiteral("terminal"), QStringLiteral("Terminal"));
#endif
    return windows;
}

} // namespace

WindowMonitor::WindowMonitor(QObject *parent)
    : QObject(parent)
    , m_windows(listWindows())
{
    m_timer.setInterval(kListMs);
    connect(&m_timer, &QTimer::timeout, this, &WindowMonitor::refresh);
    m_timer.start();
}

void WindowMonitor::refresh()
{
    m_windows = listWindows();
    emit refreshed();
}
