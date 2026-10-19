#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QHash>
#include <QMediaDevices>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <QTimer>
#include <QVector>

class QAudioSink;
class SfxMixer;

// Procedural sound-effects engine for Noch's desktop buddy. Synthesizes soft
// felt-mallet / kalimba-like tones (detuned warm body, fast-fading octave
// partial, gentle low-pass and a small stereo room) and plays them through
// one QAudioSink with a tiny software mixer.
//
// Why not QSoundEffect: it decodes temp WAV files through the multimedia
// backend's sample cache and opens the device in a fixed format. On recent
// Windows builds (Qt 6.10+/FFmpeg) that path can end up in Error or pick a
// player that never outputs anything, and after three failed recoveries
// every cue was silent for the rest of the session. Here the cues are
// rendered straight to memory, converted to the device's own preferred
// format (rate, channels, Int16/Int32/Float), and the sink is reopened on
// the next cue after any device change or error.
class BuddySfx final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(bool outputAvailable READ outputAvailable NOTIFY outputChanged)
    // Keep the sound device open between cues (the wake word chimes the moment
    // it hears its phrase, which must not wait for the device to open).
    Q_PROPERTY(bool keepOpen READ keepOpen WRITE setKeepOpen NOTIFY keepOpenChanged)

public:
    explicit BuddySfx(QObject *parent = nullptr);
    ~BuddySfx() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    double volume() const { return m_volume; }
    void setVolume(double v);

    QString style() const { return m_style; }
    void setStyle(const QString &s);

    bool keepOpen() const { return m_keepOpen; }
    void setKeepOpen(bool on);

    // True while a sound device is open (or could be opened on the next cue).
    bool outputAvailable() const { return m_outputAvailable; }

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
    // Aliases: open -> wake, success -> reply, love -> happy.
    // Returns true when the cue was let through (enabled, not rate limited).
    Q_INVOKABLE bool play(const QString &cue);
    // Opens the sound device ahead of the first cue. Opening it takes long
    // enough to stall the frame it happens in, and the device is released after
    // a quiet spell, so the UI calls this when the pointer reaches the buddy:
    // by the click the device is already open and the bubble opens smoothly.
    Q_INVOKABLE void prewarm();

    // ---- pure helpers (unit tested) -----------------------------------------
    static constexpr int kSynthRate = 44100;
    static QStringList cueNames();
    // Interleaved stereo float samples at kSynthRate, peak <= level <= 0.5,
    // starting and ending at silence.
    static QVector<float> renderCue(const QString &cue, double pitchScale);
    // Linear-interpolating resampler for interleaved stereo.
    static QVector<float> resampleStereo(const QVector<float> &in, int fromRate, int toRate);
    // Pacing: may `key` start at monotonic time `nowMs`? Records it if so.
    bool admit(const QString &key, qint64 nowMs);
    // Perceptual slider mapping.
    double outputGain() const;
    // Test hook: never touch the audio device (also set by NOCH_NO_AUDIO=1
    // and the offscreen QPA platform).
    void setDeviceDisabled(bool off) { m_deviceDisabled = off; }
    int voicesStarted() const { return m_voicesStarted; }

signals:
    void enabledChanged();
    void volumeChanged();
    void styleChanged();
    void outputChanged();
    void keepOpenChanged();

private:
    void rebuildCues();
    bool ensureSink();
    bool resolveFormat();
    void dropSink();
    void stopSoon();
    double pitchMultiplier() const;

    bool m_enabled = true;
    double m_volume = 0.75;
    QString m_style = QStringLiteral("warm"); // "cute" | "warm" | "mellow"
    bool m_deviceDisabled = false;
    bool m_keepOpen = false;
    bool m_outputAvailable = true;

    // Rendered cues at kSynthRate, and the same converted to the sink's rate.
    QHash<QString, QSharedPointer<const QVector<float>>> m_cues;
    QHash<QString, QSharedPointer<const QVector<float>>> m_deviceCues;
    int m_deviceCueRate = 0;
    QHash<QString, int> m_cueMs;

    QHash<QString, qint64> m_lastPlayMs;
    QElapsedTimer m_clock;
    QMediaDevices m_mediaDevices;
    QAudioSink *m_sink = nullptr;
    SfxMixer *m_mixer = nullptr;
    QAudioFormat m_format;
    QAudioDevice m_device;        // the device m_format was negotiated for
    bool m_formatReady = false;   // m_device / m_format are current
    QTimer m_idleTimer;           // closes the device after a quiet spell
    int m_failures = 0;           // consecutive sink open failures
    qint64 m_retryAfterMs = 0;    // back-off after a failure
    qint64 m_lastSpeakMs = -1;
    qint64 m_speakRunStartMs = 0;
    int m_lastSpeakVariant = 0;
    int m_voicesStarted = 0;
};
