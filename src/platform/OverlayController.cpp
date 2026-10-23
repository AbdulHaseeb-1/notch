#include "OverlayController.h"

#include "ShapeRegion.h"

#include <QCursor>
#include <QWindow>

OverlayController::OverlayController(QObject *parent)
    : QObject(parent)
{
}

void OverlayController::setWindow(QWindow *window)
{
    m_window = window;
    if (m_window && !m_interactiveRegion.isEmpty())
        m_window->setMask(m_interactiveRegion);
}

QPointF OverlayController::cursorPos() const
{
    const QPoint global = QCursor::pos();
    if (!m_window)
        return QPointF(global);
    return QPointF(m_window->mapFromGlobal(global));
}

void OverlayController::setWindowMask(QWindow *window, const QVariantList &regions)
{
    if (!window)
        return;
    const QRegion next = regionFromShapes(regions);
    if (window->mask() == next)
        return;
    window->setMask(next);
}

void OverlayController::setWindowGeometry(QWindow *window, int x, int y, int width, int height)
{
    if (!window)
        return;
    const QRect next(x, y, width, height);
    if (window->geometry() != next)
        window->setGeometry(next);
}

void OverlayController::setWindowPosition(QWindow *window, int x, int y)
{
    if (window && window->position() != QPoint(x, y))
        window->setPosition(x, y);
}

void OverlayController::setInteractiveRegions(const QVariantList &regions)
{
    const QRegion nextRegion = regionFromShapes(regions);

    if (nextRegion == m_interactiveRegion)
        return;
    m_interactiveRegion = nextRegion;
    if (m_window)
        m_window->setMask(m_interactiveRegion);
}
