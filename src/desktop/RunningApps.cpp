#include "RunningApps.h"

#include "WindowMonitor.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QStringList>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

constexpr int kPreviewWidth = 360;

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

RunningApps::RunningApps(WindowMonitor *windows, QObject *parent)
    : QObject(parent)
    , m_windows(windows)
{
    connect(m_windows, &WindowMonitor::refreshed, this, &RunningApps::refresh);
    refresh();
}

QString RunningApps::iconPathForId(const QString &iconId) const
{
    QMutexLocker lock(&m_iconMutex);
    return m_iconIndex.value(iconId);
}

void RunningApps::refresh()
{
    // Keep programs in the order they were first seen, so tiles do not shuffle
    // when the user clicks between windows.
    QStringList order;
    for (const QVariant &previous : std::as_const(m_apps))
        order << previous.toMap().value(QStringLiteral("exe")).toString();

    QHash<QString, WindowMonitor::Window> firstWindow;
    QHash<QString, int> counts;
    for (const WindowMonitor::Window &window : m_windows->windows()) {
        if (!window.program)
            continue;
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
        const WindowMonitor::Window &window = firstWindow.value(exe);
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

QVariantList RunningApps::windowsFor(const QString &exe)
{
    m_windows->refresh();
    QVariantList list;
    const QString wanted = exe.toLower();
    for (const WindowMonitor::Window &window : m_windows->windows()) {
        if (!window.program || window.exe != wanted)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("windowId"), window.id);
        entry.insert(QStringLiteral("title"), window.title);
        list.append(entry);
    }
    return list;
}

QVariantList RunningApps::allWindows()
{
    m_windows->refresh();
    QVariantList list;
    for (const WindowMonitor::Window &window : m_windows->windows()) {
        if (!window.program)
            continue;
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
    m_windows->refresh();
#ifdef Q_OS_WIN
    for (const WindowMonitor::Window &window : m_windows->windows()) {
        if (!window.program || window.exe != wanted)
            continue;
        const HWND hwnd = nativeWindow(window.id);
        if (hwnd && PostMessageW(hwnd, WM_CLOSE, 0, 0))
            ++closed;
    }
#else
    Q_UNUSED(exe)
#endif
    if (closed)
        m_windows->refresh(); // windows going away changes the tile set
}

bool RunningApps::activateApp(const QString &exe)
{
    const QString wanted = exe.toLower();
    m_windows->refresh();
    for (const WindowMonitor::Window &window : m_windows->windows()) {
        if (window.program && window.exe == wanted)
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
