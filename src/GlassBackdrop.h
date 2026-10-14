#pragma once

#include <QObject>

class Appearance;
class QWindow;

// Gives the notch a real backdrop blur on Windows: the window is handed to the
// system acrylic material, which blurs whatever is behind it. OverlayController
// already masks the window to the visible shapes, so the blur only shows there.
// The acrylic tint follows the light or dark theme. Where the material is not
// available blurActive stays false and the QML falls back to a denser tint.
class GlassBackdrop final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool blurActive READ blurActive NOTIFY blurActiveChanged)

public:
    GlassBackdrop(const Appearance *appearance, QObject *parent = nullptr);
    void setWindow(QWindow *window);

    bool blurActive() const { return m_blurActive; }

signals:
    void blurActiveChanged();

private:
    void applyBlur();

    const Appearance *m_appearance;
    QWindow *m_window = nullptr;
    bool m_blurActive = false;
};
