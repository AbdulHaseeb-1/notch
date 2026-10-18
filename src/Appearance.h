#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// The look of the whole app, saved with the other preferences:
//   light    dark (the default carbon) or light surfaces
//   palette  the surface family: carbon, midnight, graphite or mocha
//   accent   the highlight colour: links, focus rings, toggles, the buddy
//   ring     the desktop buddy's neon halo gradient
//
// Every palette is resolved here, once, into `colors`; Theme.qml only reads
// that map, so the dozens of Theme instances stay cheap and can never drift
// apart. GlassBackdrop reads `light` to tint the system blur to match.
class Appearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool light READ light WRITE setLight NOTIFY lightChanged)
    Q_PROPERTY(QString palette READ palette WRITE setPalette NOTIFY paletteChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QString ring READ ring WRITE setRing NOTIFY ringChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY colorsChanged)
    Q_PROPERTY(QVariantList ringColors READ ringColors NOTIFY colorsChanged)

public:
    explicit Appearance(QObject *parent = nullptr);

    bool light() const { return m_light; }
    void setLight(bool light);

    QString palette() const { return m_palette; }
    void setPalette(const QString &id);

    QString accent() const { return m_accent; }
    void setAccent(const QString &id);

    QString ring() const { return m_ring; }
    void setRing(const QString &id);

    QVariantMap colors() const { return m_colors; }
    QVariantList ringColors() const { return m_ringColors; }

    // Choices for the settings page: [{ id, label, ... preview colours }],
    // already resolved for the current light/dark mode.
    Q_INVOKABLE QVariantList paletteOptions() const;
    Q_INVOKABLE QVariantList accentOptions() const;
    Q_INVOKABLE QVariantList ringOptions() const;

    // Windows' "Animation effects" accessibility switch (Settings >
    // Accessibility > Visual effects). The desktop buddy consults it to soften
    // its idle/point animations for motion-sensitive users.
    Q_INVOKABLE bool systemMotionReduced() const;

signals:
    void lightChanged();
    void paletteChanged();
    void accentChanged();
    void ringChanged();
    void colorsChanged();

private:
    void rebuild();
    QColor accentColor(const QString &id) const;
    QVariantList ringFor(const QString &id) const;

    bool m_light = false;
    QString m_palette = QStringLiteral("carbon");
    QString m_accent = QStringLiteral("frost");
    QString m_ring = QStringLiteral("aurora");
    QVariantMap m_colors;
    QVariantList m_ringColors;
};
