#pragma once

#include <QObject>

// System appearance hints for QML.
//
// Noch has a single dark stone theme (qml/Theme.qml), so there is nothing to
// choose here any more: no light mode, surface family, accent or ring preset.
// What is left is what Windows itself says about motion.
class Appearance final : public QObject
{
    Q_OBJECT

public:
    explicit Appearance(QObject *parent = nullptr);

    // Windows' "Animation effects" accessibility switch (Settings >
    // Accessibility > Visual effects). The desktop buddy consults it to soften
    // its idle/point animations for motion-sensitive users.
    Q_INVOKABLE bool systemMotionReduced() const;
};
