#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QWindow>

class WindowMonitor;

// Watches whether a full-screen app (game, video, presentation) owns the
// foreground. The notch uses this to stay tucked away over games and to
// suppress hover reveals while they run.
//
// It also answers a second question for the music pill: is an app window sitting
// in the pill's space at the top of the screen? (`occupied`.) The pill asks by
// giving the window it lives in and the rectangle it occupies in that window's
// own coordinates; any ordinary visible app window overlapping that rectangle
// counts. Noch's own windows, minimised or cloaked windows, tool windows,
// click-through or see-through overlays and the desktop/taskbar do not.
//
// Both answers are read from the shared WindowMonitor listing, which refreshes
// once a second and on every foreground change.
class FullscreenWatcher final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The overlay window the zone is measured in, and the zone itself (in that
    // window's coordinates).
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(QRectF zone READ zone WRITE setZone NOTIFY zoneChanged)
    // An app window overlaps the zone right now, and which one (for diagnostics).
    Q_PROPERTY(bool occupied READ occupied NOTIFY occupiedChanged)
    Q_PROPERTY(QString occupiedBy READ occupiedBy NOTIFY occupiedChanged)

public:
    explicit FullscreenWatcher(WindowMonitor *windows, QObject *parent = nullptr);
    ~FullscreenWatcher() override;

    bool active() const { return m_active; }
    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    QRectF zone() const { return m_zone; }
    void setZone(const QRectF &zone);
    bool occupied() const { return m_occupied; }
    QString occupiedBy() const { return m_occupiedBy; }

public slots:
    void check();
    // Lists the windows again now and looks for an app window in the zone.
    void checkZone();

signals:
    void activeChanged();
    void windowChanged();
    void zoneChanged();
    void occupiedChanged();

private:
    void onWindowsRefreshed();
    void updateZone();

    WindowMonitor *m_windows = nullptr;
    bool m_active = false;
    QPointer<QWindow> m_window;
    QRectF m_zone;
    bool m_occupied = false;
    QString m_occupiedBy;
};
