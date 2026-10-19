#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QTimer>

#include <functional>

class QWindow;

// A global key you HOLD to talk. Windows only tells a hotkey registration
// that the combination went down, never that it came up, so after the press
// the key is polled until it is let go: pressed() when it goes down,
// released() when it comes up (two polls in a row, so a flicker does not end a
// sentence), and always released() after a maximum hold in case the key-up
// is missed. On other systems nothing is registered; the logic is the same and
// is driven by press() with a key probe, which is how it is tested.
class PushToTalkKey final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY registeredChanged)
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)

public:
    explicit PushToTalkKey(QObject *parent = nullptr);
    ~PushToTalkKey() override;

    void setWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool registered() const { return m_registered; }
    QString shortcut() const { return m_shortcut; }
    bool held() const { return m_held; }

    // Is the talk key down right now? Tests supply their own.
    void setKeyProbe(std::function<bool()> probe) { m_probe = std::move(probe); }
    void setPollIntervalMs(int ms) { m_timer.setInterval(qMax(5, ms)); }
    void setMaxHoldMs(int ms) { m_maxHoldMs = qMax(50, ms); }

    // The key went down (called from the native hotkey event; also for tests).
    Q_INVOKABLE void press();
    // Let go from the code side, e.g. the UI cancelled.
    Q_INVOKABLE void release();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void pressed();
    void released();
    void enabledChanged();
    void registeredChanged();
    void heldChanged();

private:
    void poll();
    void tryRegister();
    void deregister();

    QWindow *m_window = nullptr;
    bool m_enabled = false;
    bool m_registered = false;
    bool m_held = false;
    QString m_shortcut;
    quintptr m_hwnd = 0;
    unsigned m_vk = 0;
    std::function<bool()> m_probe;
    QTimer m_timer;
    int m_upPolls = 0;
    int m_maxHoldMs = 70000;
    qint64 m_pressedAt = 0;
};
