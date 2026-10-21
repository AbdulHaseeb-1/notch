#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class QAudioSink;
class QIODevice;

// Plays 16-bit mono PCM that arrives as a stream (the Deepgram agent's voice)
// through the default output device, with the two things a conversation needs
// that SpeechPlayer (one finished utterance at a time) does not give:
//
//  * flush(): drop everything queued and silence the device at once, so the
//    agent stops mid-word when the user talks over it (barge-in);
//  * a resampler that keeps its place between chunks, so the 24 kHz stream
//    converted to the device's rate has no click where two chunks meet.
//
// Audio is pushed to the device from a timer that runs only while there is
// something to play, so an idle player costs nothing.
class StreamingPlayer : public QObject
{
    Q_OBJECT
public:
    explicit StreamingPlayer(QObject *parent = nullptr);
    ~StreamingPlayer() override;

    // The rate of the PCM passed to write() (16-bit mono).
    void setInputRate(int hz);
    int inputRate() const { return m_inRate; }
    // 0 .. 1.5; applied as audio is converted.
    void setVolume(double v);
    // Tests (also NOCH_NO_AUDIO=1 and the offscreen platform): no device; the
    // queue drains at the speed real playback would have.
    void setDeviceDisabled(bool off);

    // Queues audio. Starts the device on the first call after silence.
    // The first audio after silence waits a moment (kPrebufferMs, or until that much has
    // arrived) before it starts, so a stream that trickles in does not stutter.
    void write(const QByteArray &pcm16);
    static constexpr int kPrebufferMs = 160;
    static constexpr int kPrebufferMaxWaitMs = 350;
    // Drops what is queued and playing. No drained().
    void flush();
    // Everything written so far has been played.
    bool idle() const { return m_queuedFrames == 0 && !m_active; }
    // Seconds written but not yet played (queued + in the device's buffer).
    double bufferedSeconds() const;
    // 0 .. 1, how loud what is playing is right now (for the speaking animation).
    double level() const { return m_level; }
    qint64 totalFrames() const { return m_totalFrames; }

    // ---- pure helper (unit tested) -------------------------------------------------
    // Converts mono 16-bit PCM at `inRate` to `to` (rate, channels, sample type),
    // continuing from `phase` (fractional input position carried between calls;
    // pass the same variable each call) and `last` (the previous chunk's last
    // sample, for interpolating across the boundary).
    static QByteArray resample(const QByteArray &pcm16, int inRate, const QAudioFormat &to, double volume,
                               double *phase, qint16 *last);

signals:
    // The queue and the device buffer ran dry after playing audio.
    void drained();
    void levelChanged();
    void failed(const QString &message);

private:
    bool openDevice();
    void closeDevice();
    void pump();
    void setActive(bool on);
    void beginPlayback();

    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    QByteArray m_queue;            // device-format bytes waiting for room in the device buffer
    qint64 m_queuedFrames = 0;     // device frames in m_queue
    QTimer m_timer;
    int m_inRate = 24000;
    double m_volume = 1.0;
    double m_phase = 0.0;
    qint16 m_last = 0;
    bool m_active = false;
    bool m_deviceDisabled = false;
    // Disabled-device simulation: the moment the last queued audio would finish.
    QElapsedTimer m_clock;
    qint64 m_simEndMs = 0;
    double m_level = 0.0;
    qint64 m_totalFrames = 0;
    int m_deviceBufferBytes = 0;
    qint64 m_dryStart = -1;
    bool m_started = false;               // playback has begun (the prebuffer was reached)
    qint64 m_prebufferDeadline = 0;
    qint64 m_simPendingMs = 0;            // disabled device: audio waiting for the prebuffer
    qint64 m_bufferedMs = 0;              // audio written since playback (re)started, in milliseconds
    qint64 m_lastLevelEmit = 0;
};
