#pragma once

#include <QPainterPath>
#include <QRect>
#include <QRegion>
#include <QVariantList>
#include <QVariantMap>

// Builds a region from the rounded rectangles the QML side describes as
// { x, y, width, height, radius } maps (device-independent pixels).
// `grow` pads every rectangle, `scale` converts to device pixels.
inline QRegion regionFromShapes(const QVariantList &shapes, qreal grow = 0, qreal scale = 1)
{
    QRegion region;
    for (const QVariant &value : shapes) {
        const QVariantMap shape = value.toMap();
        const qreal x = shape.value(QStringLiteral("x")).toDouble() - grow;
        const qreal y = shape.value(QStringLiteral("y")).toDouble() - grow;
        const qreal w = shape.value(QStringLiteral("width")).toDouble() + 2 * grow;
        const qreal h = shape.value(QStringLiteral("height")).toDouble() + 2 * grow;
        const QRect bounds(qRound(x * scale), qRound(y * scale), qRound(w * scale), qRound(h * scale));
        if (bounds.isEmpty())
            continue;
        const qreal radius = (shape.value(QStringLiteral("radius")).toDouble() + grow) * scale;
        QPainterPath path;
        path.addRoundedRect(bounds, radius, radius);
        region += QRegion(path.toFillPolygon().toPolygon(), Qt::WindingFill);
    }
    return region;
}
