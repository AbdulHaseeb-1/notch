#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <QTimer>

class QAudioSink;

// Plays synthesized speech (16-bit PCM from a TTS service) through the default
// output device, converted to the format the device mixes in: WASAPI shared mode
// only accepts its own format, and Qt does not convert for us.
//
// The whole utterance is converted up front (a few seconds of speech is a few
// hundred kilobytes) and handed to one audio sink, so there is nothing to feed
// while it plays.
class SpeechPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SpeechPlayer(QObject *parent = nullptr);
    ~SpeechPlayer() override;

    // Plays `pcm16` (interleaved, 1 or 2 channels) recorded at `rate` Hz. A
    // playing utterance is replaced. False when it cannot be played.
    bool play(const QByteArray &pcm16, int rate, int channels);
    // Stops at once, without finished().
    void stop();
    bool playing() const { return m_playing; }
    // Tests (also NOCH_NO_AUDIO=1 and the offscreen platform): never touch a device;
    // the utterance "plays" for a moment and then finishes.
    void setDeviceDisabled(bool off) { m_deviceDisabled = off; }
    int playedUtterances() const { return m_played; }
    double lastSeconds() const { return m_lastSeconds; }

    // ---- pure helpers (unit tested) ----------------------------------------------
    // The PCM out of a RIFF/WAVE file: 16-bit integer samples, 1 or 2 channels.
    // Streaming headers (a data size of 0 or 0xFFFFFFFF) read to the end.
    static QByteArray wavToPcm(const QByteArray &wav, int *rate, int *channels, QString *error);
    // 16-bit PCM -> the device's format (rate, channels, sample type), linear resampling.
    static QByteArray convert(const QByteArray &pcm16, int rate, int channels, const QAudioFormat &to);

signals:
    // The utterance played to its end.
    void finished();

private:
    void finish();
    void dropSink();

    QAudioSink *m_sink = nullptr;
    QBuffer m_buffer;
    QByteArray m_data;
    QTimer m_guard;                 // ends it even if the device never reports the end
    bool m_playing = false;
    bool m_deviceDisabled = false;
    int m_played = 0;
    double m_lastSeconds = 0;
};
