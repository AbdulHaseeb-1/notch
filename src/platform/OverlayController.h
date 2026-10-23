#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRegion>
#include <QVariantList>

class QWindow;

class OverlayController final : public QObject
{
    Q_OBJECT

public:
    explicit OverlayController(QObject *parent = nullptr);
    void setWindow(QWindow *window);

    Q_INVOKABLE void setInteractiveRegions(const QVariantList &regions);
    // Global cursor in this window's coordinates. The window mask swallows
    // moves outside the orb, so the buddy cannot track from a MouseArea.
    Q_INVOKABLE QPointF cursorPos() const;

    // The same click-through mask for another window (the buddy's own), with its
    // rounded rectangles in that window's coordinates.
    Q_INVOKABLE void setWindowMask(QWindow *window, const QVariantList &regions);
    // Moves and resizes a window in one native call, in global (screen) coordinates.
    Q_INVOKABLE void setWindowGeometry(QWindow *window, int x, int y, int width, int height);
    // Moves a window only. While the buddy is carried this is all that happens per
    // frame: the window system shifts the finished picture, nothing is drawn again.
    Q_INVOKABLE void setWindowPosition(QWindow *window, int x, int y);

private:
    QPointer<QWindow> m_window;
    QRegion m_interactiveRegion;
};
