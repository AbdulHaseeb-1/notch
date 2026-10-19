#pragma once

#include <QObject>

// Night light, on and off from the notch. Windows has no API for this, so the
// CloudStore blob the feature is backed by is rewritten exactly the way
// Windows itself does (the well-known byte pattern at offset 18 toggles the
// state and a sequence byte marks the write as fresh). If the blob does not
// look like a known-good shape, the toggle reports itself unsupported and the
// chip opens the Windows page instead.
class NightLight final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(bool on READ on NOTIFY changed)

public:
    explicit NightLight(QObject *parent = nullptr);

    bool supported() const { return m_supported; }
    bool on() const { return m_on; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void openSettings() const;

signals:
    void changed();
    void failed();

private:
    bool m_supported = false;
    bool m_on = false;
};
