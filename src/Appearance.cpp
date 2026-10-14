#include "Appearance.h"

#include <QSettings>

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
