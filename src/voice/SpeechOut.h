#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "SpeechPlayer.h"
#include "StreamingPlayer.h"

class QNetworkReply;

// Reads Buddy's reports and answers aloud. Off until switched on in Settings.
//
// It speaks a short summary (wake::spokenSummary: no code, links or Markdown, a
// few sentences), never the raw reply. Three voices:
//
//  * "system": the voice built into the computer. Windows uses System.Speech
//    through PowerShell, elsewhere espeak-ng/espeak when installed. The text goes
//    to the speech program on its standard input, never into a command line, so
//    nothing in a reply can be run as a command. Nothing leaves the computer.
//  * "google": Gemini TTS through Google AI Studio
//    (https://generativelanguage.googleapis.com). The spoken text is sent to
//    Google with the user's AI Studio API key; the audio comes back as WAV and
//    is played here.
//  * "deepgram": Deepgram Aura-2 voices through the Deepgram text-to-speech API
//    (https://api.deepgram.com). The spoken text is sent to Deepgram with the user's
//    API key; the audio comes back as WAV and is played here.
//  * "kokoro": a Kokoro TTS server (Kokoro-FastAPI or any server with an
//    OpenAI-style /v1/audio/speech), normally on this computer, so the text stays
//    here. https is required for anything that is not this computer.
//
// If the chosen voice cannot be used (no key, server down, an error) the system
// voice reads it instead, and `error` says why, so a reply is never lost silently.
class SpeechOut : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // The chosen voice can be used right now (see `problem` when not).
    Q_PROPERTY(bool available READ available NOTIFY configChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY configChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    // "system" | "google" | "deepgram" | "kokoro"
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY configChanged)
    Q_PROPERTY(QString googleKey READ googleKey WRITE setGoogleKey NOTIFY configChanged)
    Q_PROPERTY(QString googleVoice READ googleVoice WRITE setGoogleVoice NOTIFY configChanged)
    Q_PROPERTY(QString deepgramKey READ deepgramKey WRITE setDeepgramKey NOTIFY configChanged)
    Q_PROPERTY(QString deepgramVoice READ deepgramVoice WRITE setDeepgramVoice NOTIFY configChanged)
    Q_PROPERTY(QString kokoroBase READ kokoroBase WRITE setKokoroBase NOTIFY configChanged)
    Q_PROPERTY(QString kokoroVoice READ kokoroVoice WRITE setKokoroVoice NOTIFY configChanged)
    // Voices the Kokoro server advertises (GET /v1/audio/voices), for Settings.
    Q_PROPERTY(QStringList kokoroVoices READ kokoroVoices NOTIFY kokoroVoicesChanged)
    Q_PROPERTY(bool kokoroVoicesLoading READ kokoroVoicesLoading NOTIFY kokoroVoicesChanged)
    Q_PROPERTY(QString kokoroVoicesProblem READ kokoroVoicesProblem NOTIFY kokoroVoicesChanged)
    // 0.7 .. 1.5, for Google, Kokoro and (through SSML prosody) the system voice.
    // Gemini TTS has no rate parameter, so this is sent as a speaking-style hint.
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY configChanged)
    // How much of a reply is read (characters, 120 .. 2000).
    Q_PROPERTY(int maxChars READ maxChars WRITE setMaxChars NOTIFY configChanged)
    // Read every answer aloud, not only the ones asked for by voice.
    Q_PROPERTY(bool speakAll READ speakAll WRITE setSpeakAll NOTIFY configChanged)
    // Why the last reading used the system voice or failed ("" when it went well).
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
    QString deepgramKey() const { return m_deepgramKey; }
    void setDeepgramKey(const QString &key);
    QString deepgramVoice() const { return m_deepgramVoice; }
    void setDeepgramVoice(const QString &voice);
    QString kokoroBase() const { return m_kokoroBase; }
    void setKokoroBase(const QString &base);
    QString kokoroVoice() const { return m_kokoroVoice; }
    void setKokoroVoice(const QString &voice);
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    int maxChars() const { return m_maxChars; }
    void setMaxChars(int chars);
    bool speakAll() const { return m_speakAll; }
    void setSpeakAll(bool on);
    QString error() const { return m_error; }

    // The program that reads text from stdin (the system voice). Tests point it at something harmless.
    void setCommand(const QString &program, const QStringList &args);
    static void defaultCommand(QString *program, QStringList *args);
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

    // The voices the Kokoro server lists, fetched with GET /v1/audio/voices.
    QStringList kokoroVoices() const { return m_kokoroVoices; }
    bool kokoroVoicesLoading() const { return !m_voicesReply.isNull(); }
    QString kokoroVoicesProblem() const { return m_kokoroVoicesProblem; }
    Q_INVOKABLE void refreshKokoroVoices();

    // ---- pure helpers (unit tested) ----------------------------------------------
    // The URL of a Kokoro server's speech endpoint ("" if the address is unusable).
    static QString kokoroUrl(const QString &base);
    // The URL of its voice list endpoint ("" if the address is unusable).
    static QString kokoroVoicesUrl(const QString &base);
    // Why a Kokoro address cannot be used ("" if it can): https, or this computer.
    static QString kokoroAddressProblem(const QString &base);
    // Splits spoken text at sentence boundaries for pipelined synthesis:
    // short fragments merge into the next chunk, and no chunk exceeds
    // ~320 characters, so the first sound starts after one sentence.
    static QStringList splitSpeechChunks(const QString &text);

signals:
    void configChanged();
    void speakingChanged();
    void errorChanged();
    void kokoroVoicesChanged();

private:
    void start(const QString &text, bool force = false, const QString &engineOverride = QString());
    QString problemFor(const QString &engine) const;
    void startSystem(const QString &text);
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

    QProcess m_proc;
    QString m_program;
    QStringList m_args;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_voicesReply;
    StreamingPlayer m_stream;
    QPointer<QNetworkReply> m_streamReply;
    QStringList m_streamChunks;
    QString m_streamCurrent;
    QByteArray m_streamOdd;
    bool m_streamWaiting = false;   // the next sentence is held back until the player has room
    bool m_streaming = false;
    QStringList m_kokoroVoices;
    QString m_kokoroVoicesProblem;
    SpeechPlayer m_player;
    quint64 m_generation = 0;
    bool m_synthesizing = false;
    // Pipeline state (google/kokoro): chunks not yet synthesized, plus one
    // synthesized chunk buffered while the player finishes the previous one.
    bool m_pipeline = false;
    QStringList m_chunks;
    QString m_currentChunk;
    QByteArray m_bufferedPcm;
    int m_bufferedRate = 0;
    int m_bufferedChannels = 0;
    bool m_lastSpeaking = false;
    bool m_enabled = false;
    QString m_engine = QStringLiteral("system");
    QString m_activeEngine = QStringLiteral("system");   // the engine the current utterance uses
    QString m_googleKey;
    QString m_googleVoice = QStringLiteral("Kore");
    QString m_deepgramKey;
    QString m_deepgramVoice = QStringLiteral("aura-2-thalia-en");
    QString m_deepgramBase = QStringLiteral("https://api.deepgram.com");
    QString m_googleEndpoint = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/interactions");
    QString m_kokoroBase = QStringLiteral("http://127.0.0.1:8880");
    QString m_kokoroVoice = QStringLiteral("af_heart");
    double m_speed = 1.0;
    int m_maxChars = kDefaultMaxChars;
    bool m_speakAll = false;
    QString m_error;
};
