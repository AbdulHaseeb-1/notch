#pragma once

#include <QImage>
#include <QPainter>

// Paints rounded corners into a copy of the image by erasing everything
// outside a rounded rectangle. Used by the cover-art providers so QML can
// show circular / rounded artwork without a QML module that clips children.
inline QImage roundedImageCorners(const QImage &source, qreal radiusFraction)
{
    if (source.isNull())
        return {};
    const QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0)
        return {};
    const qreal radius = qBound<qreal>(0.0, radiusFraction, 0.5) * qMin(w, h);
    if (radius <= 0.5)
        return img;

    QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter mp(&mask);
    mp.setRenderHint(QPainter::Antialiasing);
    mp.setPen(Qt::NoPen);
    mp.setBrush(Qt::white);
    mp.drawRoundedRect(QRectF(0, 0, w, h), radius, radius);
    mp.end();

    QImage out = img;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
    p.end();
    return out;
}
