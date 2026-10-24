#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "DeepgramSettings.h"
#include "SpeechPlayer.h"
#include "StreamingPlayer.h"

class QNetworkReply;

// Reads Buddy's reports and answers aloud. Off until switched on in Settings.
//
// It speaks a short summary (wake::spokenSummary: no code, links or Markdown, a
// few sentences), never the raw reply. Two voices, both cloud services:
//
//  * "google": Gemini TTS through Google AI Studio
//    (https://generativelanguage.googleapis.com). The spoken text is sent to
//    Google with the user's AI Studio API key; the audio comes back as WAV and
//    is played here.
//  * "deepgram": Deepgram Aura-2 voices through the Deepgram text-to-speech API
//    (https://api.deepgram.com). The spoken text is sent to Deepgram with the user's
//    API key; the audio comes back as WAV and is played here.
//
// If the chosen voice cannot be used (no key, service down, an error) nothing
// is spoken and `error` says why: there is no fallback voice.
class SpeechOut : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // The chosen voice can be used right now (see `problem` when not).
    Q_PROPERTY(bool available READ available NOTIFY configChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY configChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    // "google" | "deepgram"
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY configChanged)
    Q_PROPERTY(QString googleKey READ googleKey WRITE setGoogleKey NOTIFY configChanged)
    Q_PROPERTY(QString googleVoice READ googleVoice WRITE setGoogleVoice NOTIFY configChanged)
    // 0.7 .. 1.5. Gemini TTS has no rate parameter, so for Google this is sent
    // as a speaking-style hint; Deepgram takes it as its speed parameter.
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY configChanged)
    // How much of a reply is read (characters, 120 .. 2000).
    Q_PROPERTY(int maxChars READ maxChars WRITE setMaxChars NOTIFY configChanged)
    // Read every answer aloud, not only the ones asked for by voice.
    Q_PROPERTY(bool speakAll READ speakAll WRITE setSpeakAll NOTIFY configChanged)
    // Why the last reading failed ("" when it went well).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kDefaultMaxChars = 280;

    explicit SpeechOut(QObject *parent = nullptr);
    ~SpeechOut() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool available() const { return problem().isEmpty(); }
    QString problem() const;
    bool speaking() const;

    QString engine() const { return m_engine; }
    void setEngine(const QString &engine);
    QString googleKey() const { return m_googleKey; }
    void setGoogleKey(const QString &key);
    QString googleVoice() const { return m_googleVoice; }
    void setGoogleVoice(const QString &voice);
    // The one Deepgram configuration, shared with push-to-talk and the live
    // agent: the Deepgram key and voice come from it, not from properties
    // of this class. Until one is attached, a private default is used.
    void setDeepgramSettings(DeepgramSettings *settings);
    DeepgramSettings *deepgramSettings() { return m_settings ? m_settings : &m_fallbackSettings; }
    const DeepgramSettings *deepgramSettings() const { return m_settings ? m_settings : &m_fallbackSettings; }
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    int maxChars() const { return m_maxChars; }
    void setMaxChars(int chars);
    bool speakAll() const { return m_speakAll; }
    void setSpeakAll(bool on);
    QString error() const { return m_error; }

    // Tests: where the Google request goes (the real one is fixed) and no real sound.
    void setGoogleEndpoint(const QString &url) { m_googleEndpoint = url; }
    void setDeepgramApiBase(const QString &base) { m_deepgramBase = base; }
    SpeechPlayer *player() { return &m_player; }
    // The Deepgram voice streams into this one, as the audio arrives.
    StreamingPlayer *streamPlayer() { return &m_stream; }

    // Say a reply (summarised). Does nothing when off or unavailable. A new one replaces the old.
    Q_INVOKABLE void speakReport(const QString &reply);
    // Say exactly this short line ("I need your approval").
    Q_INVOKABLE void say(const QString &line);
    // Say it even when the setting is off (the Settings "Test voice" button).
    Q_INVOKABLE void test();
    // The same, with the Deepgram voice whatever engine is chosen (Settings > Deepgram).
    Q_INVOKABLE void previewDeepgram();
    // What would be read aloud for this reply (the short summary, no code or links).
    Q_INVOKABLE QString summary(const QString &reply) const;
    Q_INVOKABLE void stop();
    // What the settings shows about the voices: {id: {ready, detail}}.
    Q_INVOKABLE QVariantMap engineStatuses() const;

    // ---- pure helpers (unit tested) ----------------------------------------------
    // Splits spoken text at sentence boundaries for pipelined synthesis:
    // short fragments merge into the next chunk, and no chunk exceeds
    // ~320 characters, so the first sound starts after one sentence.
    static QStringList splitSpeechChunks(const QString &text);

signals:
    void configChanged();
    void speakingChanged();
    void errorChanged();

private:
    void start(const QString &text, bool force = false, const QString &engineOverride = QString());
    QString problemFor(const QString &engine) const;
    // Cloud voices speak in sentence chunks: chunk i+1 is synthesized while
    // chunk i plays, so time-to-first-sound is one sentence's synthesis.
    void beginPipeline(const QString &text);
    void synthesizeNext();
    // Deepgram speaks sentence by sentence straight into a streaming player: the first
    // sound starts when the first bytes arrive, not when the whole sentence is ready.
    void beginDeepgramStream(const QString &text);
    void streamNextChunk();
    void onStreamFailure(const QString &why);
    void stopStream();
    void onChunkAudio(const QByteArray &pcm, int rate, int channels);
    void onPlayerChunkFinished();
    QString remainingSpeech() const;   // current chunk + everything unsaid
    void onAudio(const QByteArray &wav, const QString &text);
    void onFailure(const QString &why, const QString &text);
    void setError(const QString &why);
    void setSpeaking();

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    StreamingPlayer m_stream;
    QPointer<QNetworkReply> m_streamReply;
    QStringList m_streamChunks;
    QString m_streamCurrent;
    QByteArray m_streamOdd;
    bool m_streamWaiting = false;   // the next sentence is held back until the player has room
    bool m_streaming = false;
    SpeechPlayer m_player;
    quint64 m_generation = 0;
    bool m_synthesizing = false;
    // Pipeline state (google): chunks not yet synthesized, plus one
    // synthesized chunk buffered while the player finishes the previous one.
    bool m_pipeline = false;
    QStringList m_chunks;
    QString m_currentChunk;
    QByteArray m_bufferedPcm;
    int m_bufferedRate = 0;
    int m_bufferedChannels = 0;
    bool m_lastSpeaking = false;
    bool m_enabled = false;
    QString m_engine = QStringLiteral("google");
    QString m_activeEngine = QStringLiteral("google");   // the engine the current utterance uses
    QString m_googleKey;
    QString m_googleVoice = QStringLiteral("Kore");
    DeepgramSettings *m_settings = nullptr;
    DeepgramSettings m_fallbackSettings;
    QString m_deepgramBase = QStringLiteral("https://api.deepgram.com");
    QString m_googleEndpoint = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/interactions");
    double m_speed = 1.0;
    int m_maxChars = kDefaultMaxChars;
    bool m_speakAll = false;
    QString m_error;
};
