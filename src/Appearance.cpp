#include "Appearance.h"

#include <QSettings>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
const char kLightKey[] = "ui/lightTheme";
}

Appearance::Appearance(QObject *parent)
    : QObject(parent)
{
    const QSettings settings;
    m_light = settings.value(QLatin1String(kLightKey), false).toBool();
}

void Appearance::setLight(bool light)
{
    if (m_light == light)
        return;
    m_light = light;
    QSettings settings;
    settings.setValue(QLatin1String(kLightKey), m_light);
    emit lightChanged();
}

bool Appearance::systemMotionReduced() const
{
#ifdef Q_OS_WIN
    // SPI_GETCLIENTAREAANIMATION reflects the user's "Animation effects"
    // toggle; when it is off, background/client-area animations should calm
    // down. Unsupported or failing queries default to "animations on".
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return !enabled;
#endif
    return false;
}
