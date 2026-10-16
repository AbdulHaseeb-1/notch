#pragma once

#include <QObject>
#include <QTimer>

// Watches whether a full-screen app (game, video, presentation) owns the
// foreground. The notch uses this to stay tucked away over games and to
// suppress hover reveals while they run.
class FullscreenWatcher final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit FullscreenWatcher(QObject *parent = nullptr);

    bool active() const { return m_active; }

signals:
    void activeChanged();

private:
    void check();

    bool m_active = false;
    QTimer m_timer;
};
