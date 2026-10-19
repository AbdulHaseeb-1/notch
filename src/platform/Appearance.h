#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// System appearance hints for QML.
//
// Noch has a single dark stone theme (qml/Theme.qml): no light mode, surface
// family or accent picker. Two things are still chosen or read here:
//   ring  the desktop buddy's multi-colour neon halo (Settings > Buddy >
//         Ring colour), saved as "buddy/ring"; default "aurora"
//   what Windows itself says about motion.
class Appearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString ring READ ring WRITE setRing NOTIFY ringChanged)
    Q_PROPERTY(QVariantList ringColors READ ringColors NOTIFY ringChanged)

public:
    explicit Appearance(QObject *parent = nullptr);

    QString ring() const { return m_ring; }
    void setRing(const QString &id);

    // Four gradient stops around the halo (the gradient closes on the first).
    QVariantList ringColors() const { return m_ringColors; }

    // Choices for the settings page: [{ id, label, colors }].
    Q_INVOKABLE QVariantList ringOptions() const;
    static QVariantList ringFor(const QString &id);

    // Windows' "Animation effects" accessibility switch (Settings >
    // Accessibility > Visual effects). The desktop buddy consults it to soften
    // its idle/point animations for motion-sensitive users.
    Q_INVOKABLE bool systemMotionReduced() const;

signals:
    void ringChanged();

private:
    QString m_ring = QStringLiteral("aurora");
    QVariantList m_ringColors;
};
