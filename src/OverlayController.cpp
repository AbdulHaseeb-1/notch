#include "OverlayController.h"

#include "ShapeRegion.h"

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

void OverlayController::setInteractiveRegions(const QVariantList &regions)
{
    const QRegion nextRegion = regionFromShapes(regions);

    if (nextRegion == m_interactiveRegion)
        return;
    m_interactiveRegion = nextRegion;
    if (m_window)
        m_window->setMask(m_interactiveRegion);
}
