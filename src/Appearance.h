#pragma once

#include <QObject>

// The one appearance setting: dark (the default black glass) or light (pale
// glass). Saved with the other preferences, and read by Theme.qml and by
// GlassBackdrop, which must tint the system blur to match.
class Appearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool light READ light WRITE setLight NOTIFY lightChanged)

public:
    explicit Appearance(QObject *parent = nullptr);

    bool light() const { return m_light; }
    void setLight(bool light);

    // Windows' "Animation effects" accessibility switch (Settings >
    // Accessibility > Visual effects). The desktop buddy consults it to soften
    // its idle/point animations for motion-sensitive users.
    Q_INVOKABLE bool systemMotionReduced() const;

signals:
    void lightChanged();

private:
    bool m_light = false;
};
