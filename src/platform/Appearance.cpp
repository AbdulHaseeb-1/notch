#include "Appearance.h"

#include <QColor>
#include <QCoreApplication>
#include <QSettings>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

const char kRingKey[] = "buddy/ring";

// The buddy's neon halo: four stops around the ring (the gradient closes
// back on the first). Same presets, names and order as before the stone
// overhaul. "accent" used to follow the accent picker; that picker is gone,
// so it is built from the old default accent (Frost, #81a1c1) instead.
struct RingDef {
    const char *id;
    const char *label;
    const char *stops[4];
};

const RingDef kRings[] = {
    { "aurora", QT_TRANSLATE_NOOP("Appearance", "Aurora"), { "#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b" } },
    { "sunset", QT_TRANSLATE_NOOP("Appearance", "Sunset"), { "#ff5f7e", "#ff9a5c", "#ffd36b", "#ff7ac6" } },
    { "ocean",  QT_TRANSLATE_NOOP("Appearance", "Ocean"),  { "#3ee8ff", "#3b82f6", "#6c7cff", "#22d3ee" } },
    { "mint",   QT_TRANSLATE_NOOP("Appearance", "Mint"),   { "#7cf5c9", "#3ee8ff", "#a3f77c", "#5eead4" } },
    { "candy",  QT_TRANSLATE_NOOP("Appearance", "Candy"),  { "#ff4fd8", "#ff8fb1", "#c58bff", "#8b6cff" } },
    { "mono",   QT_TRANSLATE_NOOP("Appearance", "Silver"), { "#ffffff", "#9aa3b2", "#e6e9ef", "#6b7280" } },
    { "accent", QT_TRANSLATE_NOOP("Appearance", "Accent"), { nullptr, nullptr, nullptr, nullptr } },
};
const char kAccentBase[] = "#81a1c1";

const RingDef *findRing(const QString &id)
{
    for (const RingDef &r : kRings)
        if (id == QLatin1String(r.id))
            return &r;
    return nullptr;
}

} // namespace

Appearance::Appearance(QObject *parent)
    : QObject(parent)
{
    // The old theme pickers are gone; drop their saved values so the
    // settings file does not carry dead keys around forever. The ring
    // colour is still a choice, so "buddy/ring" stays.
    QSettings settings;
    for (const char *key : { "ui/lightTheme", "ui/palette", "ui/accent" }) {
        if (settings.contains(QLatin1String(key)))
            settings.remove(QLatin1String(key));
    }
    const QString ring = settings.value(QLatin1String(kRingKey)).toString();
    if (findRing(ring))
        m_ring = ring;
    m_ringColors = ringFor(m_ring);
}

void Appearance::setRing(const QString &id)
{
    if (m_ring == id || !findRing(id))
        return;
    m_ring = id;
    m_ringColors = ringFor(m_ring);
    QSettings().setValue(QLatin1String(kRingKey), m_ring);
    emit ringChanged();
}

QVariantList Appearance::ringFor(const QString &id)
{
    QVariantList out;
    const RingDef *r = findRing(id);
    if (!r)
        r = &kRings[0];
    if (r->stops[0]) {
        for (const char *c : r->stops)
            out << QColor(QLatin1String(c));
        return out;
    }
    // Accent ring: the accent's own glow, from a bright tint to a deep shade.
    const QColor a = QColor(QLatin1String(kAccentBase)).toHsl();
    const auto shade = [&a](double dl, double ds, int dh) {
        return QColor::fromHslF(std::fmod(a.hslHueF() + dh / 360.0 + 1.0, 1.0),
                                std::clamp(a.hslSaturationF() + ds, 0.0, 1.0),
                                std::clamp(a.lightnessF() + dl, 0.0, 1.0));
    };
    out << shade(0.10, 0.15, 0) << shade(-0.08, 0.20, 18)
        << shade(0.22, 0.10, -10) << shade(-0.02, 0.25, -24);
    return out;
}

QVariantList Appearance::ringOptions() const
{
    QVariantList out;
    for (const RingDef &r : kRings) {
        out << QVariantMap{ { QStringLiteral("id"), QLatin1String(r.id) },
                            { QStringLiteral("label"), QCoreApplication::translate("Appearance", r.label) },
                            { QStringLiteral("colors"), ringFor(QLatin1String(r.id)) } };
    }
    return out;
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
