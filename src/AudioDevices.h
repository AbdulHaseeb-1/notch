#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

// Sound output chips for the System page: every active playback device, which
// one is the default, and switching between them. Also the default microphone's
// mute, which Windows hides two menus deep.
class AudioDevices final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(bool hasMic READ hasMic NOTIFY micChanged)
    Q_PROPERTY(bool micMuted READ micMuted NOTIFY micChanged)
    Q_PROPERTY(qreal masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(bool masterMuted READ masterMuted NOTIFY masterVolumeChanged)

public:
    explicit AudioDevices(QObject *parent = nullptr);
    ~AudioDevices() override;

    // One entry per device: id, name, isDefault.
    QVariantList outputs() const { return m_outputs; }
    bool hasMic() const { return m_hasMic; }
    bool micMuted() const { return m_micMuted; }
    qreal masterVolume() const { return m_masterVolume; }
    bool masterMuted() const { return m_masterMuted; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool setDefault(const QString &id);
    Q_INVOKABLE void toggleMicMute();
    Q_INVOKABLE void setMasterVolume(qreal volume);
    Q_INVOKABLE void toggleMasterMute();

signals:
    void outputsChanged();
    void micChanged();
    void masterVolumeChanged();

private:
    void poll();

    QVariantList m_outputs;
    bool m_hasMic = false;
    bool m_micMuted = false;
    qreal m_masterVolume = 0.8;
    bool m_masterMuted = false;
    QTimer m_poll;
};
