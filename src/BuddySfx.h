#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QObject>
#include <QSoundEffect>
#include <QString>
#include <QTimer>

// Procedural sound-effects engine for Noch's desktop buddy. Synthesizes soft
// felt-mallet / kalimba-like tones (detuned warm body, fast-fading octave
// partial, gentle low-pass and a small stereo room) into temp WAV files that
// are loaded into low-latency QSoundEffect instances.
class BuddySfx final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)

public:
    explicit BuddySfx(QObject *parent = nullptr);
    ~BuddySfx() override = default;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    double volume() const { return m_volume; }
    void setVolume(double v);

    QString style() const { return m_style; }
    void setStyle(const QString &s);

    // Supported cues:
    //   "wake"    - soft bubble bloop + rising third as the thought bubble opens
    //   "pop"     - tiny water-drop pop (the trailing thought dots)
    //   "dismiss" - the bubble folding back in, a soft falling pair
    //   "think"   - hushed breathing two-note swell when a request is sent
    //   "reply"   - warm unhurried arpeggio when an answer arrives
    //   "happy"   - little pentatonic giggle when petted or loved
    //   "drop"    - cushioned thup when set down after dragging
    //   "speak"   - quiet syllable blips (3 variants) while an answer reveals
    //   "error"   - sympathetic sagging minor third
    Q_INVOKABLE void play(const QString &cue);

signals:
    void enabledChanged();
    void volumeChanged();
    void styleChanged();

private:
    void rebuildEffects();
    void scheduleRecovery();
    void recoverEffects();
    void stopEffects();
    double pitchMultiplier() const;
    double outputGain() const;
    static QByteArray synthesizeCue(const QString &cue, double pitchScale);

    bool m_enabled = true;
    double m_volume = 0.75;
    QString m_style = QStringLiteral("warm"); // "cute" | "warm" | "mellow"
    QHash<QString, QSoundEffect *> m_effects;
    QHash<QString, int> m_cueMs;         // how long each cue sounds, in ms
    QHash<QString, qint64> m_lastPlayMs; // monotonic time each cue was last started
    QElapsedTimer m_clock;
    QMediaDevices m_mediaDevices;
    QTimer m_recoveryTimer;
    int m_recoveryAttempts = 0;
    qint64 m_lastSpeakMs = -1;           // last "speak" attempt, played or not
    qint64 m_speakRunStartMs = 0;        // start of the current run of blips
    int m_lastSpeakVariant = 0;
};
