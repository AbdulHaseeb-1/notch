#include "ComputerUse.h"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QScreen>
#include <QtMath>

// ---- CoordinateMapper ------------------------------------------------------

QSize CoordinateMapper::agentSizeFor(const QSize &logical, const QSize &maxSize)
{
    if (logical.isEmpty())
        return maxSize;
    if (logical.width() <= maxSize.width() && logical.height() <= maxSize.height())
        return logical;
    const double s = qMin(double(maxSize.width()) / logical.width(), double(maxSize.height()) / logical.height());
    return QSize(qBound(1, qRound(logical.width() * s), maxSize.width()),
                 qBound(1, qRound(logical.height() * s), maxSize.height()));
}

QList<ScreenInfo> CoordinateMapper::qtScreens()
{
    QList<ScreenInfo> out;
    const QScreen *primary = QGuiApplication::primaryScreen();
    for (QScreen *s : QGuiApplication::screens()) {
        ScreenInfo i;
        i.geometry = s->geometry();
        i.dpr = s->devicePixelRatio();
        // Qt keeps each screen's origin in native pixels and scales within
        // the screen, so the physical top-left equals the logical one.
        i.nativeOrigin = s->geometry().topLeft();
        i.name = s->name();
        i.primary = s == primary;
        out << i;
    }
    return out;
}

void CoordinateMapper::setScreens(const QList<ScreenInfo> &screens)
{
    const QString was = m_display >= 0 && m_display < m_screens.size() ? m_screens.at(m_display).name : QString();
    m_screens = screens;
    m_display = 0;
    for (int i = 0; i < m_screens.size(); ++i)
        if (m_screens.at(i).primary)
            m_display = i;
    for (int i = 0; i < m_screens.size(); ++i)   // keep the display the agent was looking at
        if (!was.isEmpty() && m_screens.at(i).name == was)
            m_display = i;
}

bool CoordinateMapper::setDisplay(int index)
{
    if (index < 0 || index >= m_screens.size())
        return false;
    m_display = index;
    return true;
}

ScreenInfo CoordinateMapper::current() const
{
    if (m_display >= 0 && m_display < m_screens.size())
        return m_screens.at(m_display);
    ScreenInfo fallback;
    fallback.geometry = QRect(0, 0, 1280, 800);
    fallback.primary = true;
    return fallback;
}

QSize CoordinateMapper::agentSize() const
{
    return cu::fitToLimits(current().geometry.size(), m_limits);
}

QPoint CoordinateMapper::agentToLogical(const QPoint &a) const
{
    const QRect g = current().geometry;
    const QSize as = agentSize();
    const int ax = qBound(0, a.x(), as.width() - 1);
    const int ay = qBound(0, a.y(), as.height() - 1);
    // Centre of the agent pixel, so logical -> agent -> logical is stable.
    const int lx = int(std::floor((ax + 0.5) * g.width() / as.width()));
    const int ly = int(std::floor((ay + 0.5) * g.height() / as.height()));
    return g.topLeft() + QPoint(qBound(0, lx, g.width() - 1), qBound(0, ly, g.height() - 1));
}

QPoint CoordinateMapper::logicalToAgent(const QPoint &l) const
{
    const QRect g = current().geometry;
    const QSize as = agentSize();
    const int ax = int(std::floor((l.x() - g.x() + 0.5) * as.width() / qMax(1, g.width())));
    const int ay = int(std::floor((l.y() - g.y() + 0.5) * as.height() / qMax(1, g.height())));
    return QPoint(qBound(0, ax, as.width() - 1), qBound(0, ay, as.height() - 1));
}

QRect CoordinateMapper::agentRectToLogical(const QRect &a) const
{
    const QRect g = current().geometry;
    const QSize as = agentSize();
    const double sx = double(g.width()) / as.width();
    const double sy = double(g.height()) / as.height();
    QRect r(g.x() + qRound(a.x() * sx), g.y() + qRound(a.y() * sy), qRound(a.width() * sx), qRound(a.height() * sy));
    return r.intersected(g);
}

int CoordinateMapper::screenAt(const QPoint &l) const
{
    for (int i = 0; i < m_screens.size(); ++i)
        if (m_screens.at(i).geometry.contains(l))
            return i;
    return -1;
}

QPoint CoordinateMapper::logicalToPhysical(const QPoint &l) const
{
    const int i = screenAt(l);
    const ScreenInfo s = i >= 0 ? m_screens.at(i) : current();
    const QPoint rel = l - s.geometry.topLeft();
    return s.nativeOrigin + QPoint(qRound(rel.x() * s.dpr), qRound(rel.y() * s.dpr));
}

QPoint CoordinateMapper::physicalToLogical(const QPoint &p) const
{
    for (const ScreenInfo &s : m_screens) {
        const QRect native(s.nativeOrigin, QSize(qRound(s.geometry.width() * s.dpr), qRound(s.geometry.height() * s.dpr)));
        if (native.contains(p)) {
            const QPoint rel = p - s.nativeOrigin;
            return s.geometry.topLeft() + QPoint(qRound(rel.x() / s.dpr), qRound(rel.y() / s.dpr));
        }
    }
    return p;
}

// ---- RateLimiter -------------------------------------------------------------

bool RateLimiter::tryAcquire(qint64 now, qint64 *retryIn)
{
    while (!m_stamps.isEmpty() && now - m_stamps.first() >= m_windowMs)
        m_stamps.removeFirst();
    qint64 wait = 0;
    if (!m_stamps.isEmpty() && now - m_stamps.last() < m_minGapMs)
        wait = m_minGapMs - (now - m_stamps.last());
    if (m_stamps.size() >= m_burst)
        wait = qMax(wait, m_windowMs - (now - m_stamps.first()));
    if (wait > 0) {
        if (retryIn)
            *retryIn = wait;
        return false;
    }
    m_stamps.append(now);
    return true;
}

// ---- SimulatedBackend --------------------------------------------------------

void SimulatedBackend::moveTo(const QPoint &p)
{
    m_cursor = p;
    log << QStringLiteral("move %1,%2").arg(p.x()).arg(p.y());
}

void SimulatedBackend::button(Qt::MouseButton b, bool down)
{
    const QString n = b == Qt::RightButton ? QStringLiteral("right") : b == Qt::MiddleButton ? QStringLiteral("middle")
                                                                                              : QStringLiteral("left");
    log << QStringLiteral("%1 %2").arg(n, down ? QStringLiteral("down") : QStringLiteral("up"));
}

void SimulatedBackend::scroll(int dx, int dy)
{
    log << QStringLiteral("scroll %1,%2").arg(dx).arg(dy);
}

bool SimulatedBackend::typeText(const QString &text)
{
    log << QStringLiteral("type %1").arg(text);
    return true;
}

bool SimulatedBackend::keyCombo(const cu::KeyCombo &combo)
{
    log << QStringLiteral("key %1").arg(combo.toString());
    return true;
}

bool SimulatedBackend::keyState(const cu::KeyCombo &combo, bool down)
{
    log << QStringLiteral("%1 %2").arg(down ? QStringLiteral("keydown") : QStringLiteral("keyup"), combo.toString());
    return true;
}

QImage SimulatedBackend::grab(const ScreenInfo &screen)
{
    if (useRealScreens) {
        for (QScreen *s : QGuiApplication::screens()) {
            if (s->name() == screen.name) {
                const QImage img = s->grabWindow(0).toImage();
                if (!img.isNull())
                    return img;
            }
        }
    }
    // Test card at physical resolution: a gradient with a grid, so scaling
    // and cropping are visible.
    const QSize phys(qMax(1, qRound(screen.geometry.width() * screen.dpr)),
                     qMax(1, qRound(screen.geometry.height() * screen.dpr)));
    QImage img(phys, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, phys.width(), phys.height());
    g.setColorAt(0, QColor("#2c2a27"));
    g.setColorAt(1, QColor("#57534d"));
    p.fillRect(img.rect(), g);
    p.setPen(QColor(255, 255, 255, 40));
    for (int x = 0; x < phys.width(); x += 64)
        p.drawLine(x, 0, x, phys.height());
    for (int y = 0; y < phys.height(); y += 64)
        p.drawLine(0, y, phys.width(), y);
    return img;
}

std::unique_ptr<ComputerBackend> makeDefaultBackend()
{
#ifdef Q_OS_WIN
    return makeWindowsBackend();
#else
    return std::make_unique<SimulatedBackend>();
#endif
}
