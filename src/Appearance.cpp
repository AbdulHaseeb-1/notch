#include "Appearance.h"

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

const char kLightKey[] = "ui/lightTheme";
const char kPaletteKey[] = "ui/palette";
const char kAccentKey[] = "ui/accent";
const char kRingKey[] = "buddy/ring";

// Surface families. Each row lists, in order:
//   surface content raised hover press tile tileEdge tileActive track frame
//   field fieldBorder border borderSoft borderStrong orb
// "orb" is the buddy's body: it stays dark in every mode (it's a character,
// not a surface), only tinted to sit well with the family around it.
const char *const kSurfaceKeys[] = {
    "surface", "content", "raised", "hover", "press", "tile", "tileEdge", "tileActive",
    "track", "frame", "field", "fieldBorder", "border", "borderSoft", "borderStrong", "orb"
};
constexpr int kSurfaceCount = int(sizeof(kSurfaceKeys) / sizeof(kSurfaceKeys[0]));

struct PaletteDef {
    const char *id;
    const char *label;
    const char *dark[kSurfaceCount];
    const char *light[kSurfaceCount];
};

const PaletteDef kPalettes[] = {
    { "carbon", QT_TRANSLATE_NOOP("Appearance", "Carbon"),
      { "#121214", "#18181b", "#1b1b1f", "#222227", "#2a2a30", "#232329", "#303038", "#2d2d35",
        "#2b2b33", "#242429", "#141417", "#2e2e36", "#28282f", "#212127", "#3a3a44", "#0e0e12" },
      { "#f4f4f6", "#ffffff", "#ffffff", "#ececf0", "#e2e2e8", "#eaeaef", "#d8d8e0", "#dedee6",
        "#dcdce4", "#e4e4ea", "#f7f7fa", "#d0d0da", "#dadadd", "#e5e5eb", "#c2c2ce", "#0e0e12" } },
    { "midnight", QT_TRANSLATE_NOOP("Appearance", "Midnight"),
      { "#0f1322", "#141a2c", "#171e31", "#1e2740", "#26304b", "#1f2840", "#2c3753", "#29344f",
        "#273150", "#1c2438", "#11172a", "#2a3550", "#232c44", "#1b2338", "#36425f", "#0a0e1b" },
      { "#eef2fa", "#ffffff", "#ffffff", "#e3e9f5", "#d8e0ef", "#e2e8f4", "#cdd6e8", "#d6deee",
        "#d2dbec", "#dde4f2", "#f5f8fd", "#c7d1e5", "#d3dbea", "#e1e7f3", "#b5c1d9", "#0a0e1b" } },
    { "graphite", QT_TRANSLATE_NOOP("Appearance", "Graphite"),
      { "#1a1a1c", "#202023", "#242427", "#2b2b2f", "#333338", "#2c2c30", "#39393f", "#36363b",
        "#34343a", "#2a2a2e", "#1c1c1f", "#38383e", "#303035", "#28282c", "#44444b", "#121214" },
      { "#ededee", "#fafafa", "#fafafa", "#e4e4e6", "#dadadd", "#e2e2e5", "#cfcfd4", "#d7d7db",
        "#d4d4d9", "#dedee1", "#f3f3f4", "#c9c9cf", "#d4d4d8", "#e0e0e3", "#b9b9c0", "#121214" } },
    { "mocha", QT_TRANSLATE_NOOP("Appearance", "Mocha"),
      { "#17130f", "#1d1814", "#211b16", "#29221c", "#322a22", "#2a231d", "#3a3028", "#352c24",
        "#342b23", "#261f19", "#19140f", "#3a3028", "#2e261f", "#241e18", "#4a3d32", "#120e0b" },
      { "#f6f1ea", "#fffdf9", "#fffdf9", "#efe7dc", "#e6dccf", "#ede4d8", "#dccfbf", "#e4d8c9",
        "#e0d4c5", "#eae1d5", "#faf6f0", "#d6c8b6", "#e0d5c7", "#ebe3d8", "#c7b6a1", "#120e0b" } },
};

// Accents: a softer tone for dark surfaces, a deeper one for light ones so
// text and focus rings keep their contrast.
struct AccentDef {
    const char *id;
    const char *label;
    const char *dark;
    const char *light;
};

const AccentDef kAccents[] = {
    { "frost",  QT_TRANSLATE_NOOP("Appearance", "Frost"),  "#81a1c1", "#2b6cb0" },
    { "ocean",  QT_TRANSLATE_NOOP("Appearance", "Ocean"),  "#5cc8e8", "#14799d" },
    { "violet", QT_TRANSLATE_NOOP("Appearance", "Violet"), "#a78bfa", "#6d4ad6" },
    { "rose",   QT_TRANSLATE_NOOP("Appearance", "Rose"),   "#f28cb1", "#c23d6f" },
    { "coral",  QT_TRANSLATE_NOOP("Appearance", "Coral"),  "#ff9a76", "#c4502e" },
    { "amber",  QT_TRANSLATE_NOOP("Appearance", "Amber"),  "#f1b467", "#a86412" },
    { "mint",   QT_TRANSLATE_NOOP("Appearance", "Mint"),   "#6fd6a8", "#1f8a5c" },
};

// The buddy's neon halo: four stops around the ring (the gradient closes
// back on the first). "accent" is built from the current accent instead.
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

template <typename T, size_t N>
const T *findById(const T (&table)[N], const QString &id)
{
    for (const T &row : table) {
        if (id == QLatin1String(row.id))
            return &row;
    }
    return nullptr;
}

} // namespace

Appearance::Appearance(QObject *parent)
    : QObject(parent)
{
    const QSettings settings;
    m_light = settings.value(QLatin1String(kLightKey), false).toBool();
    const QString palette = settings.value(QLatin1String(kPaletteKey)).toString();
    if (findById(kPalettes, palette))
        m_palette = palette;
    const QString accent = settings.value(QLatin1String(kAccentKey)).toString();
    if (findById(kAccents, accent))
        m_accent = accent;
    const QString ring = settings.value(QLatin1String(kRingKey)).toString();
    if (findById(kRings, ring))
        m_ring = ring;
    rebuild();
}

void Appearance::setLight(bool light)
{
    if (m_light == light)
        return;
    m_light = light;
    QSettings settings;
    settings.setValue(QLatin1String(kLightKey), m_light);
    rebuild();
    emit lightChanged();
}

void Appearance::setPalette(const QString &id)
{
    if (m_palette == id || !findById(kPalettes, id))
        return;
    m_palette = id;
    QSettings().setValue(QLatin1String(kPaletteKey), m_palette);
    rebuild();
    emit paletteChanged();
}

void Appearance::setAccent(const QString &id)
{
    if (m_accent == id || !findById(kAccents, id))
        return;
    m_accent = id;
    QSettings().setValue(QLatin1String(kAccentKey), m_accent);
    rebuild();
    emit accentChanged();
}

void Appearance::setRing(const QString &id)
{
    if (m_ring == id || !findById(kRings, id))
        return;
    m_ring = id;
    QSettings().setValue(QLatin1String(kRingKey), m_ring);
    rebuild();
    emit ringChanged();
}

QColor Appearance::accentColor(const QString &id) const
{
    const AccentDef *a = findById(kAccents, id);
    if (!a)
        a = &kAccents[0];
    return QColor(QLatin1String(m_light ? a->light : a->dark));
}

QVariantList Appearance::ringFor(const QString &id) const
{
    QVariantList out;
    const RingDef *r = findById(kRings, id);
    if (!r)
        r = &kRings[0];
    if (r->stops[0]) {
        for (const char *c : r->stops)
            out << QColor(QLatin1String(c));
        return out;
    }
    // Accent ring: the accent's own glow, from a bright tint to a deep shade.
    const QColor a = accentColor(m_accent).toHsl();
    const auto shade = [&a](double dl, double ds, int dh) {
        QColor c = QColor::fromHslF(std::fmod(a.hslHueF() + dh / 360.0 + 1.0, 1.0),
                                    std::clamp(a.hslSaturationF() + ds, 0.0, 1.0),
                                    std::clamp(a.lightnessF() + dl, 0.0, 1.0));
        return c;
    };
    out << shade(0.10, 0.15, 0) << shade(-0.08, 0.20, 18)
        << shade(0.22, 0.10, -10) << shade(-0.02, 0.25, -24);
    return out;
}

void Appearance::rebuild()
{
    const PaletteDef *p = findById(kPalettes, m_palette);
    if (!p)
        p = &kPalettes[0];
    QVariantMap c;
    for (int i = 0; i < kSurfaceCount; ++i)
        c.insert(QLatin1String(kSurfaceKeys[i]),
                 QColor(QLatin1String(m_light ? p->light[i] : p->dark[i])));
    const QColor accent = accentColor(m_accent);
    c.insert(QStringLiteral("accent"), accent);
    c.insert(QStringLiteral("accentHover"), m_light ? accent.lighter(112) : accent.lighter(110));
    m_colors = c;
    m_ringColors = ringFor(m_ring);
    emit colorsChanged();
}

QVariantList Appearance::paletteOptions() const
{
    QVariantList out;
    for (const PaletteDef &p : kPalettes) {
        const char *const *row = m_light ? p.light : p.dark;
        out << QVariantMap{ { QStringLiteral("id"), QLatin1String(p.id) },
                            { QStringLiteral("label"), tr(p.label) },
                            { QStringLiteral("surface"), QColor(QLatin1String(row[0])) },
                            { QStringLiteral("raised"), QColor(QLatin1String(row[2])) },
                            { QStringLiteral("border"), QColor(QLatin1String(row[14])) } };
    }
    return out;
}

QVariantList Appearance::accentOptions() const
{
    QVariantList out;
    for (const AccentDef &a : kAccents) {
        out << QVariantMap{ { QStringLiteral("id"), QLatin1String(a.id) },
                            { QStringLiteral("label"), tr(a.label) },
                            { QStringLiteral("color"), QColor(QLatin1String(m_light ? a.light : a.dark)) } };
    }
    return out;
}

QVariantList Appearance::ringOptions() const
{
    QVariantList out;
    for (const RingDef &r : kRings) {
        out << QVariantMap{ { QStringLiteral("id"), QLatin1String(r.id) },
                            { QStringLiteral("label"), tr(r.label) },
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
