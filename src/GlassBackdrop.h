#pragma once

#include <QObject>
#include <QPointer>
#include <QRegion>
#include <QVariantList>

class QWindow;

// Optional real blur behind the glass ("Blur behind", off by default).
//
// The notch window spans the screen and is transparent, so a window-wide blur
// (the system acrylic material) would fog the whole desktop. Instead this asks
// the compositor to blur only the rounded shapes the QML reports through
// setShapes(), using DwmEnableBlurBehindWindow with an explicit blur region.
// While it is off, or where the effect is not available, blurActive stays
// false and the QML draws a denser, opaque-enough glass instead.
class GlassBackdrop final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool blurActive READ blurActive NOTIFY blurActiveChanged)

public:
    explicit GlassBackdrop(QObject *parent = nullptr);
    void setWindow(QWindow *window);

    bool supported() const;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool blurActive() const { return m_blurActive; }

    // The visible glass shapes as { x, y, width, height, radius } maps.
    Q_INVOKABLE void setShapes(const QVariantList &shapes);

signals:
    void enabledChanged();
    void blurActiveChanged();

private:
    void apply();

    QPointer<QWindow> m_window;
    QRegion m_shapes;
    bool m_enabled = false;
    bool m_blurActive = false;
    bool m_applied = false;     // blur-behind has been touched at least once
};
