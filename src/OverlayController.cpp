#include "OverlayController.h"

#include <QPainterPath>
#include <QRect>
#include <QVariantMap>
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
    QRegion nextRegion;

    for (const QVariant &value : regions) {
        const QVariantMap region = value.toMap();
        const QRect bounds(qRound(region.value(QStringLiteral("x")).toDouble()),
                           qRound(region.value(QStringLiteral("y")).toDouble()),
                           qRound(region.value(QStringLiteral("width")).toDouble()),
                           qRound(region.value(QStringLiteral("height")).toDouble()));
        if (bounds.isEmpty())
            continue;

        const qreal radius = region.value(QStringLiteral("radius")).toDouble();
        QPainterPath path;
        path.addRoundedRect(bounds, radius, radius);
        nextRegion += QRegion(path.toFillPolygon().toPolygon(), Qt::WindingFill);
    }

    if (nextRegion == m_interactiveRegion)
        return;
    m_interactiveRegion = nextRegion;
    if (m_window)
        m_window->setMask(m_interactiveRegion);
}
