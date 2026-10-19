#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

class QWindow;

// The summon shortcut: Ctrl+Alt+Space brings up the notch's command palette
// from anywhere. Registered on the notch window itself, so the hotkey dies
// with the window and never outlives the app.
class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    // The combination actually registered ("Ctrl+Alt+Space" unless another app
    // owns that one, in which case Noch falls back to a quieter combo).
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY shortcutChanged)
    Q_PROPERTY(bool preferred READ preferred NOTIFY shortcutChanged)

public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    void setWindow(QWindow *window);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool registered() const { return m_registered; }
    QString shortcut() const { return m_shortcut; }
    bool preferred() const { return m_preferred; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();
    void enabledChanged();
    void registeredChanged();
    void shortcutChanged();

private:
    void tryRegister();
    void unregister();
    void deregisterWindow();

    QWindow *m_window = nullptr;
    bool m_enabled = true;
    bool m_registered = false;
    bool m_preferred = true;
    QString m_shortcut;
    // The HWND the hotkey is registered on, cached at register time.
    quintptr m_hwnd = 0;
};
