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
        if (radius <= 1.0) {
            region += bounds;
        } else {
            QPainterPath path;
            path.addRoundedRect(bounds, radius, radius);
            region += QRegion(path.toFillPolygon().toPolygon(), Qt::WindingFill);
        }
    }
    return region;
}

// The mask a window gets from a list of shapes. An empty region would clear the window's
// mask, and then Windows lets the window take mouse input over all of its area (the notch
// window covers the whole screen), so "nothing clickable" is a rectangle outside the window.
inline QRegion maskFromShapes(const QVariantList &shapes, qreal grow = 0, qreal scale = 1)
{
    const QRegion region = regionFromShapes(shapes, grow, scale);
    return region.isEmpty() ? QRegion(QRect(-1, -1, 1, 1)) : region;
}
