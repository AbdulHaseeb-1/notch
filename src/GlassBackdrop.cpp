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
    m_enabled = false;
}

bool GlassBackdrop::supported() const
{
    return false;
}

void GlassBackdrop::setWindow(QWindow *window)
{
    m_window = window;
}

void GlassBackdrop::setEnabled(bool enabled)
{
    Q_UNUSED(enabled);
    if (m_enabled) {
        m_enabled = false;
        emit enabledChanged();
    }
}

void GlassBackdrop::setShapes(const QVariantList &shapes)
{
    Q_UNUSED(shapes);
}

void GlassBackdrop::apply()
{
}

