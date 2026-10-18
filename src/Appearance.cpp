#include "Appearance.h"

#include <QSettings>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

Appearance::Appearance(QObject *parent)
    : QObject(parent)
{
    // The old theme pickers are gone; drop their saved values so the
    // settings file does not carry dead keys around forever.
    QSettings settings;
    for (const char *key : { "ui/lightTheme", "ui/palette", "ui/accent", "buddy/ring" }) {
        if (settings.contains(QLatin1String(key)))
            settings.remove(QLatin1String(key));
    }
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
