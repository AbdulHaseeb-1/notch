#include "GlassBackdrop.h"

#include "ShapeRegion.h"

#include <QSettings>
#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace {
const char kEnabledKey[] = "ui/blurBehind";

#ifdef Q_OS_WIN
// Settings > Personalization > Colors > "Transparency effects". When it is off
// the blur request succeeds but nothing is blurred.
bool transparencyEnabled()
{
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("EnableTransparency"), 1).toInt() != 0;
}

HRGN toWinRegion(const QRegion &region)
{
    HRGN result = CreateRectRgn(0, 0, 0, 0);
    for (const QRect &r : region) {
        HRGN piece = CreateRectRgn(r.left(), r.top(), r.left() + r.width(), r.top() + r.height());
        CombineRgn(result, result, piece, RGN_OR);
        DeleteObject(piece);
    }
    return result;
}
#endif
} // namespace

GlassBackdrop::GlassBackdrop(QObject *parent)
    : QObject(parent)
{
    const QSettings settings;
    m_enabled = settings.value(QLatin1String(kEnabledKey), false).toBool();
}

bool GlassBackdrop::supported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void GlassBackdrop::setWindow(QWindow *window)
{
    m_window = window;
    apply();
}

void GlassBackdrop::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings settings;
    settings.setValue(QLatin1String(kEnabledKey), m_enabled);
    emit enabledChanged();
    apply();
}

void GlassBackdrop::setShapes(const QVariantList &shapes)
{
    if (!m_enabled)
        return; // nothing to track while the blur is off
    const qreal scale = m_window ? m_window->devicePixelRatio() : 1.0;
    const QRegion next = regionFromShapes(shapes, 0, scale);
    if (next == m_shapes)
        return;
    m_shapes = next;
    apply();
}

void GlassBackdrop::apply()
{
#ifdef Q_OS_WIN
    if (!m_window)
        return;

    const bool want = m_enabled && transparencyEnabled() && !m_shapes.isEmpty();
    // Until the blur has been asked for once, leave the window exactly as Qt
    // made it. Qt itself relies on DWM blur-behind with an empty region to keep
    // per-pixel transparency working, so "off" must put that back instead of
    // switching blur-behind off.
    if (!want && !m_applied) {
        if (m_blurActive) {
            m_blurActive = false;
            emit blurActiveChanged();
        }
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    HRGN region = want ? toWinRegion(m_shapes) : CreateRectRgn(0, 0, -1, -1);
    DWM_BLURBEHIND blur{};
    blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    blur.fEnable = TRUE;
    blur.hRgnBlur = region;
    const bool ok = SUCCEEDED(DwmEnableBlurBehindWindow(hwnd, &blur));
    DeleteObject(region);
    m_applied = true;

    const bool active = want && ok;
    if (active != m_blurActive) {
        m_blurActive = active;
        emit blurActiveChanged();
    }
#endif
}
