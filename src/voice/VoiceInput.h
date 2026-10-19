#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

// Where the microphone's audio comes from. The real one is Qt Multimedia;
// tests use a fake that is fed bytes directly.
class AudioCapture : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // Opens the microphone. False (with a message) if there is none or it will not open.
    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;

signals:
    // 16-bit mono 16 kHz PCM, as it is recorded.
    void audio(const QByteArray &pcm);
    void failed(const QString &message);
};

std::unique_ptr<AudioCapture> makeMicrophoneCapture();

// Push-to-talk voice input. Hold to talk: beginListening() opens the
// microphone, endListening() closes it and sends the recording to an
// OpenAI-compatible /v1/audio/transcriptions endpoint (OpenAI, Groq, a local
// whisper server, ...); the words come back through transcript().
//
// Privacy, because this is a microphone:
//  * off until the user turns it on; nothing is recorded while it is off;
//  * recording only while the key / button is held, capped at maxSeconds;
//  * audio lives in memory, is sent once and then dropped - never written to disk;
//  * a clip with nothing in it (muted mic) is not sent anywhere;
//  * audio is only sent over https, or to this machine;
//  * the state is always visible (state, level) so the UI can show the mic is live.
class VoiceInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY configChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY configChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY configChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY configChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    // "idle" | "listening" | "transcribing"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY levelChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr double kMinSeconds = 0.35;
    static constexpr int kDefaultMaxSeconds = 60;

    explicit VoiceInput(QObject *parent = nullptr);
    ~VoiceInput() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString apiBase() const { return m_apiBase; }
    void setApiBase(const QString &b);
    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &k);
    QString model() const { return m_model; }
    void setModel(const QString &m);
    QString language() const { return m_language; }
    void setLanguage(const QString &l);
    bool configured() const;

    QString state() const;
    bool listening() const { return m_state == State::Listening; }
    bool busy() const { return m_state != State::Idle; }
    double level() const { return m_level; }
    double seconds() const;
    QString error() const { return m_error; }

    void setCapture(std::unique_ptr<AudioCapture> capture);
    void setMaxSeconds(int s) { m_maxSeconds = qMax(1, s); }
    // The full URL the audio is posted to (empty if the address is unusable).
    Q_INVOKABLE QString transcriptionUrl() const;
    // Why the address cannot be used ("" if it can).
    Q_INVOKABLE QString addressProblem() const;
    // Is the speech service on this computer (so audio never leaves it)?
    Q_INVOKABLE bool localService() const;

    // One transcription of a clip, by the same service and key as push-to-talk.
    struct Transcription {
        bool ok = false;
        bool empty = false;      // the service answered, with no words in it
        int status = 0;          // HTTP status (0: no answer)
        QString text;
        QString error;           // user-readable when !ok
    };
    // Sends `wav` without touching push-to-talk's state. `done` runs once on
    // `context`'s thread (never, if `context` is destroyed first). The reply can be aborted.
    QPointer<QNetworkReply> transcribe(const QByteArray &wav, QObject *context,
                                       std::function<void(const Transcription &)> done);

    // Start recording. False, with `error` set, when voice is off or not set up or the mic will not open.
    Q_INVOKABLE bool beginListening();
    // Stop recording and transcribe it.
    Q_INVOKABLE void endListening();
    // Stop and throw the recording away (or abandon the request in flight).
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

signals:
    void transcript(const QString &text);
    void failed(const QString &message);
    void stateChanged();
    void levelChanged();
    void errorChanged();
    void configChanged();

private:
    enum class State { Idle, Listening, Transcribing };
    void setState(State s);
    void fail(const QString &message);
    void onAudio(const QByteArray &pcm);
    void onReply();
    void send(const QByteArray &wav);
    QNetworkReply *post(const QByteArray &wav);
    static Transcription parse(QNetworkReply *reply);
    static bool isLocalAddress(const QString &base);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<AudioCapture> m_capture;
    QTimer m_maxTimer;
    QByteArray m_pcm;
    State m_state = State::Idle;
    double m_level = 0;
    QString m_error;
    bool m_enabled = false;
    QString m_apiBase = QStringLiteral("https://api.openai.com");
    QString m_apiKey;
    QString m_model = QStringLiteral("whisper-1");
    QString m_language;
    int m_maxSeconds = kDefaultMaxSeconds;
    quint64 m_generation = 0;
};
