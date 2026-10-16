#include "RunningApps.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QSet>
#include <QStringList>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace {

constexpr int kRefreshMs = 2000;
constexpr int kPreviewWidth = 360;

struct OpenWindow {
    double id = 0;
    QString title;
    QString exe;  // lower-case file name, e.g. "chrome.exe"
    QString path; // full path of the program
};

QString iconIdFor(const QString &path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
}

QString displayName(const QString &path)
{
    QString name = QFileInfo(path).completeBaseName();
    if (!name.isEmpty())
        name[0] = name.at(0).toUpper();
    return name.isEmpty() ? path : name;
}

#ifdef Q_OS_WIN
QString programPath(DWORD pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[4096];
    DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, static_cast<int>(size));
    CloseHandle(process);
    return path;
}

// Visible, user-facing top-level windows only: no tool windows, owned dialogs,
// cloaked (other virtual desktop) windows, or our own.
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM lParam)
{
    auto *out = reinterpret_cast<QList<OpenWindow> *>(lParam);
    if (!IsWindowVisible(hwnd))
        return TRUE;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool appWindow = (style & WS_EX_APPWINDOW) != 0;
    if (!appWindow && ((style & WS_EX_TOOLWINDOW) || GetWindow(hwnd, GW_OWNER)))
        return TRUE;

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    constexpr int kTitleChars = 512;
    wchar_t buffer[kTitleChars];
    const int length = GetWindowTextW(hwnd, buffer, kTitleChars);
    if (length <= 0)
        return TRUE;
    const QString title = QString::fromWCharArray(buffer, length);
    if (title == QLatin1String("Program Manager"))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId())
        return TRUE;

    const QString path = programPath(pid);
    if (path.isEmpty())
        return TRUE;

    OpenWindow window;
    window.id = double(reinterpret_cast<quintptr>(hwnd));
    window.title = title;
    window.path = path;
    window.exe = QFileInfo(path).fileName().toLower();
    out->append(window);
    return TRUE;
}

HWND nativeWindow(double windowId)
{
    return reinterpret_cast<HWND>(static_cast<quintptr>(windowId));
}

// PrintWindow asks the window to paint itself, so it works for windows that
// are not on top. PW_RENDERFULLCONTENT also captures GPU-drawn content
// (browsers, games). Hung windows are skipped so the capture cannot block.
QImage captureWindow(HWND hwnd)
{
    constexpr UINT kRenderFullContent = 0x00000002;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) || IsHungAppWindow(hwnd))
        return {};
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return {};

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);

    QImage image;
    if (PrintWindow(hwnd, memory, kRenderFullContent)) {
        image = QImage(width, height, QImage::Format_RGB32);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // top-down rows
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        // GetDIBits must not find the bitmap selected into the DC it is
        // given (documented requirement), so put the old bitmap back first.
        if (!SelectObject(memory, previous)) {
            image = QImage();
        } else if (!GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), image.bits(), &info,
                              DIB_RGB_COLORS)) {
            image = QImage();
        }
    }

    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return image;
}
#endif

QList<OpenWindow> enumerateWindows()
{
    QList<OpenWindow> windows;
#ifdef Q_OS_WIN
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&windows));
#else
    // Placeholder windows so the layout can be exercised off Windows. The path
    // is the name, so the icon falls back to the monogram.
    windows << OpenWindow{1, QStringLiteral("Claude - New chat"), QStringLiteral("claude"),
                          QStringLiteral("Claude")}
            << OpenWindow{2, QStringLiteral("main.cpp - notch"), QStringLiteral("cursor"),
                          QStringLiteral("Cursor")}
            << OpenWindow{3, QStringLiteral("README.md - notch"), QStringLiteral("cursor"),
                          QStringLiteral("Cursor")}
            << OpenWindow{4, QStringLiteral("bash - notch"), QStringLiteral("terminal"),
                          QStringLiteral("Terminal")};
#endif
    return windows;
}

QImage placeholderPreview(double windowId)
{
    QImage image(320, 200, QImage::Format_RGB32);
    // static_cast<int>(double) on a value past INT_MAX is undefined, so do the
    // arithmetic in integer types and fold the remainder into range.
    const int hue = int((static_cast<quintptr>(windowId) * 67) % 360);
    QLinearGradient gradient(0, 0, image.width(), image.height());
    gradient.setColorAt(0.0, QColor::fromHsv(hue, 110, 95));
    gradient.setColorAt(1.0, QColor::fromHsv((hue + 40) % 360, 170, 45));
    QPainter painter(&image);
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("Preview"));
    return image;
}

} // namespace

RunningApps::RunningApps(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(kRefreshMs);
    connect(&m_timer, &QTimer::timeout, this, &RunningApps::refresh);
    m_timer.start();
    refresh();
}

QString RunningApps::iconPathForId(const QString &iconId) const
{
    QMutexLocker lock(&m_iconMutex);
    return m_iconIndex.value(iconId);
}

void RunningApps::refresh()
{
    const QList<OpenWindow> windows = enumerateWindows();

    // Keep programs in the order they were first seen, so tiles do not shuffle
    // when the user clicks between windows.
    QStringList order;
    for (const QVariant &previous : std::as_const(m_apps))
        order << previous.toMap().value(QStringLiteral("exe")).toString();

    QHash<QString, OpenWindow> firstWindow;
    QHash<QString, int> counts;
    for (const OpenWindow &window : windows) {
        if (!counts.contains(window.exe)) {
            firstWindow.insert(window.exe, window);
            if (!order.contains(window.exe))
                order << window.exe;
        }
        counts[window.exe] += 1;
    }

    QVariantList apps;
    QHash<QString, QString> icons;
    QStringList signature;
    for (const QString &exe : std::as_const(order)) {
        if (!counts.contains(exe))
            continue;
        const OpenWindow &window = firstWindow.value(exe);
        const QString iconId = iconIdFor(window.path);
        QVariantMap app;
        app.insert(QStringLiteral("name"), displayName(window.path));
        app.insert(QStringLiteral("exe"), exe);
        app.insert(QStringLiteral("path"), QDir::toNativeSeparators(window.path));
        app.insert(QStringLiteral("iconId"), iconId);
        app.insert(QStringLiteral("count"), counts.value(exe));
        apps.append(app);
        icons.insert(iconId, window.path);
        signature << exe + QLatin1Char(':') + QString::number(counts.value(exe));
    }

    {
        QMutexLocker lock(&m_iconMutex);
        // Merge, don't replace: a heavily-cached program can restart at the
        // same exe/path and get a different paged-out iconId mapping between
        // the sampled app list and this index otherwise. Retired paths simply
        // linger (a handful of QStrings) and honest lookups still win.
        for (auto it = icons.constBegin(); it != icons.constEnd(); ++it)
            m_iconIndex.insert(it.key(), it.value());
    }

    // Titles change often (browser tabs, documents); the tiles only need to
    // change when a program opens, closes or gains or loses a window.
    const QString next = signature.join(QLatin1Char(','));
    if (next != m_signature) {
        m_signature = next;
        m_apps = apps;
        emit appsChanged();
    }
}

QVariantList RunningApps::windowsFor(const QString &exe) const
{
    QVariantList list;
    const QString wanted = exe.toLower();
    for (const OpenWindow &window : enumerateWindows()) {
        if (window.exe != wanted)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("windowId"), window.id);
        entry.insert(QStringLiteral("title"), window.title);
        list.append(entry);
    }
    return list;
}

QVariantList RunningApps::allWindows() const
{
    QVariantList list;
    for (const OpenWindow &window : enumerateWindows()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("windowId"), window.id);
        entry.insert(QStringLiteral("title"), window.title);
        entry.insert(QStringLiteral("exe"), window.exe);
        entry.insert(QStringLiteral("name"), displayName(window.path));
        entry.insert(QStringLiteral("path"), QDir::toNativeSeparators(window.path));
        entry.insert(QStringLiteral("iconId"), iconIdFor(window.path));
        list.append(entry);
    }
    return list;
}

void RunningApps::closeApp(const QString &exe)
{
    const QString wanted = exe.toLower();
    int closed = 0;
#ifdef Q_OS_WIN
    for (const OpenWindow &window : enumerateWindows()) {
        if (window.exe != wanted)
            continue;
        const HWND hwnd = nativeWindow(window.id);
        if (hwnd && PostMessageW(hwnd, WM_CLOSE, 0, 0))
            ++closed;
    }
#else
    Q_UNUSED(exe)
#endif
    if (closed)
        refresh(); // windows going away changes the tile set
}

bool RunningApps::activateApp(const QString &exe)
{
    const QString wanted = exe.toLower();
    for (const OpenWindow &window : enumerateWindows()) {
        if (window.exe == wanted)
            return activateWindow(window.id);
    }
    return false;
}

bool RunningApps::activateWindow(double windowId)
{
#ifdef Q_OS_WIN
    const HWND hwnd = nativeWindow(windowId);
    if (!IsWindow(hwnd))
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    return SetForegroundWindow(hwnd) != FALSE;
#else
    Q_UNUSED(windowId);
    return true;
#endif
}

RunningPreviewProvider::RunningPreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage RunningPreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize);
    const double windowId = id.section(QLatin1Char('/'), 0, 0).toDouble();

    QImage image;
#ifdef Q_OS_WIN
    image = captureWindow(nativeWindow(windowId));
#endif
    if (image.isNull())
        image = placeholderPreview(windowId);

    if (image.width() > kPreviewWidth)
        image = image.scaled(kPreviewWidth, kPreviewWidth, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
