#include "AppIconProvider.h"

#include "AppSearch.h"
#include "DockController.h"
#include "RunningApps.h"

#include <QAbstractFileIconProvider>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <commctrl.h>
#  include <commoncontrols.h>
#endif

namespace {

constexpr int kIconExtent = 128;
// Each cached icon is a 128px ARGB32 image (~64 KB): a dock with a dozen
// pinned apps plus every currently open program already touches dozens, and
// apps cycle in and out, so cap how much sticks around instead of growing
// with the user's whole session.
constexpr int kCacheLimit = 64;

// Precomputed 256-entry lookup tables for Rec. 709 luminance adjustments.
// Replaces 16,384 std::pow calls per 128x128 icon with an instant L1-cache lookup.
static const auto s_monoLut = []() {
    std::array<int, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const double l = i / 255.0;
        const double lifted = 0.08 + 0.92 * std::pow(l, 0.82);
        lut[i] = std::clamp(int(std::lround(lifted * 255.0)), 0, 255);
    }
    return lut;
}();

static const auto s_inkLut = []() {
    std::array<int, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const double l = i / 255.0;
        const double ink = 0.06 + 0.6 * std::pow(l, 0.9);
        lut[i] = std::clamp(int(std::lround(ink * 255.0)), 0, 255);
    }
    return lut;
}();

#ifdef Q_OS_WIN
using PrivateExtractIconsWFn = UINT(WINAPI *)(LPCWSTR, int, int, int, HICON *, UINT *, UINT, UINT);

// PrivateExtractIconsW returns the large (up to 256px) frames that
// SHGetFileInfo can't. It is resolved at run time to avoid depending on SDK
// headers declaring it.
QImage extractLargeIcon(const QString &file, int extent)
{
    static const auto fn = reinterpret_cast<PrivateExtractIconsWFn>(reinterpret_cast<void *>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "PrivateExtractIconsW")));
    if (!fn)
        return {};

    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    for (int size : {256, extent, 64, 48}) {
        HICON icon = nullptr;
        UINT iconId = 0;
        const UINT count = fn(path.c_str(), 0, size, size, &icon, &iconId, 1, 0);
        if (count == 1 && icon) {
            QImage image = QImage::fromHICON(icon);
            DestroyIcon(icon);
            if (!image.isNull())
                return image;
        }
    }
    return {};
}

// The shell's 256 px "jumbo" image list: sharp icons for things that are
// not an exe/ico/dll (Store apps, documents, .url shortcuts). Files that only
// ship a small icon come back centred on a transparent canvas, which
// trimTransparent() removes afterwards.
QImage jumboShellIcon(const QString &file)
{
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX))
        return {};
    // IID_IImageList {46EB5926-582E-4017-9FDF-E8998DAA0950}, spelled out so
    // no extra import library is needed.
    static const GUID kIidImageList = { 0x46eb5926, 0x582e, 0x4017,
                                        { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };
    IImageList *list = nullptr;
    if (FAILED(SHGetImageList(SHIL_JUMBO, kIidImageList, reinterpret_cast<void **>(&list))) || !list)
        return {};
    HICON icon = nullptr;
    const HRESULT hr = list->GetIcon(info.iIcon, ILD_TRANSPARENT, &icon);
    list->Release();
    if (FAILED(hr) || !icon)
        return {};
    QImage image = QImage::fromHICON(icon);
    DestroyIcon(icon);
    return image;
}

QImage shellIcon(const QString &file)
{
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
        return {};
    QImage image;
    if (info.hIcon) {
        image = QImage::fromHICON(info.hIcon);
        DestroyIcon(info.hIcon);
    }
    return image;
}
#endif

// Trim fully transparent borders so icons with generous padding line up
// visually with icons that fill their canvas.
QImage trimTransparent(const QImage &src)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    int left = img.width(), right = -1, top = img.height(), bottom = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < left || bottom < top)
        return src;
    const int w = right - left + 1;
    const int h = bottom - top + 1;
    const int side = std::max(w, h);
    QImage square(side, side, QImage::Format_ARGB32_Premultiplied);
    square.fill(Qt::transparent);
    QPainter p(&square);
    p.drawImage((side - w) / 2, (side - h) / 2, img, left, top, w, h);
    p.end();
    return square;
}

} // namespace

AppIconProvider::AppIconProvider(const DockController *dock, const RunningApps *running,
                                 const AppSearch *search)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_dock(dock)
    , m_running(running)
    , m_search(search)
{
}

// Caller must hold m_mutex. Drops the oldest entry of the named ring if the
// cap is exceeded. `which` selects among the colour / mono / ink caches.
void AppIconProvider::insertCached(const QString &file, const QImage &image, const char *which)
{
    QStringList *order = nullptr;
    if (which == "colour") {
        m_cache.insert(file, image);
        order = &m_cacheOrder;
    } else if (which == "mono") {
        m_monoCache.insert(file, image);
        order = &m_monoOrder;
    } else {
        m_inkCache.insert(file, image);
        order = &m_inkOrder;
    }
    if (!order->contains(file))
        order->append(file);
    if (order->size() > kCacheLimit) {
        const QString retire = order->takeFirst();
        if (which == "colour")
            m_cache.remove(retire);
        else if (which == "mono")
            m_monoCache.remove(retire);
        else
            m_inkCache.remove(retire);
    }
}

// Colour icon for a file, loaded once. The caller must hold m_mutex.
QImage AppIconProvider::colourIcon(const QString &file)
{
    auto it = m_cache.constFind(file);
    if (it != m_cache.constEnd())
        return it.value();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto failIt = m_failedLookups.constFind(file);
    if (failIt != m_failedLookups.constEnd() && (now - failIt.value() < 5000))
        return {};

    const QImage image = loadIcon(file, kIconExtent);
    if (!image.isNull()) {
        insertCached(file, image, "colour");
        m_failedLookups.remove(file);
    } else {
        m_failedLookups.insert(file, now);
    }
    return image;
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString mode = id.section(QLatin1Char('/'), 0, 0);
    const QString iconId = id.section(QLatin1Char('/'), 1);
    QString file = m_dock ? m_dock->iconPathForId(iconId) : QString();
    if (file.isEmpty() && m_running)
        file = m_running->iconPathForId(iconId);
    if (file.isEmpty() && m_search)
        file = m_search->iconPathForId(iconId);

    QImage image;
    if (!file.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        const bool onLight = mode == QLatin1String("monoLight");
        if (onLight || mode == QLatin1String("mono")) {
            QHash<QString, QImage> &cache = onLight ? m_inkCache : m_monoCache;
            auto it = cache.constFind(file);
            if (it != cache.constEnd()) {
                image = it.value();
            } else {
                const QImage colour = colourIcon(file);
                if (!colour.isNull())
                    image = onLight ? toInkMonochrome(colour) : toMonochrome(colour);
                // Same rule as colourIcon(): failures stay re-retryable.
                if (!image.isNull())
                    insertCached(file, image, onLight ? "ink" : "mono");
            }
        } else {
            image = colourIcon(file);
        }
    }

    if (image.isNull()) {
        if (size)
            *size = QSize();
        return {};
    }

    if (requestedSize.isValid() && !requestedSize.isEmpty() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size)
        *size = image.size();
    return image;
}

QImage AppIconProvider::loadIcon(const QString &file, int extent)
{
    QImage image;
#ifdef Q_OS_WIN
    // Shortcuts: prefer the target's high-resolution icon.
    QString source = file;
    const QFileInfo fi(file);
    if (fi.suffix().compare(QLatin1String("lnk"), Qt::CaseInsensitive) == 0) {
        const QString target = fi.symLinkTarget();
        if (!target.isEmpty() && QFileInfo::exists(target))
            source = target;
    }
    const QString suffix = QFileInfo(source).suffix().toLower();
    if (suffix == QLatin1String("exe") || suffix == QLatin1String("ico")
        || suffix == QLatin1String("dll")) {
        image = extractLargeIcon(source, extent);
    }
    if (image.isNull())
        image = jumboShellIcon(file);
    if (image.isNull())
        image = shellIcon(file);
#endif
    if (image.isNull()) {
        const QAbstractFileIconProvider provider;
        const QIcon icon = provider.icon(QFileInfo(file));
        if (!icon.isNull())
            image = icon.pixmap(extent, extent).toImage();
    }
    if (image.isNull())
        return {};

    image = trimTransparent(image);
    if (image.width() != extent)
        image = image.scaled(extent, extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage AppIconProvider::toMonochrome(const QImage &source)
{
    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a == 0)
                continue;
            // Rec. 709 luminance with precomputed lift LUT
            const int lum = int(0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x])
                               + 0.0722 * qBlue(line[x]) + 0.5);
            const int v = s_monoLut[std::clamp(lum, 0, 255)];
            line[x] = qRgba(v, v, v, a);
        }
    }
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Same luminance detail as toMonochrome, but drawn dark so it reads on pale
// glass: black logos stay near black and white logos become mid grey.
QImage AppIconProvider::toInkMonochrome(const QImage &source)
{
    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a == 0)
                continue;
            const int lum = int(0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x])
                               + 0.0722 * qBlue(line[x]) + 0.5);
            const int v = s_inkLut[std::clamp(lum, 0, 255)];
            line[x] = qRgba(v, v, v, a);
        }
    }
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
