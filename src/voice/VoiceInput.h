#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "Transcriber.h"

class TranscribeWorker;

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
// microphone, endListening() closes it and transcribes the recording on this
// computer with Moonshine Tiny (see Moonshine.h); the words come back through
// transcript().
//
// Privacy, because this is a microphone:
//  * off until the user turns it on; nothing is recorded while it is off;
//  * recording only while the key / button is held, capped at maxSeconds;
//  * audio lives in memory and is transcribed by a model on this computer: it is
//    never sent anywhere and never written to disk;
//  * a clip with nothing in it (muted mic) is not transcribed;
//  * the state is always visible (state, level) so the UI can show the mic is live.
class VoiceInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // The speech model's files are on this computer (see `problem` when not).
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY configChanged)
    // The model is loaded and ready; false while it starts (a moment after turning voice on).
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString engine READ engine CONSTANT)
    Q_PROPERTY(QString modelFolder READ modelFolder CONSTANT)
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
    bool configured() const;
    QString problem() const;
    bool ready() const { return m_ready; }
    QString engine() const;
    QString modelFolder() const { return m_modelFolder; }

    QString state() const;
    bool listening() const { return m_state == State::Listening; }
    bool busy() const { return m_state != State::Idle; }
    double level() const { return m_level; }
    double seconds() const;
    QString error() const { return m_error; }

    void setCapture(std::unique_ptr<AudioCapture> capture);
    void setMaxSeconds(int s) { m_maxSeconds = qMax(1, s); }
    // Another speech-to-text engine (tests use a scripted one). Takes effect for the next request.
    void setTranscriber(std::unique_ptr<Transcriber> transcriber);
    Q_INVOKABLE void openModelFolder() const;
    // What the settings shows about the speech model: the engine name,
    // whether it is usable, and its files with their state.
    Q_INVOKABLE QVariantMap sttModelStatus() const;

    // One transcription of a clip (16 kHz mono 16-bit PCM), by the same engine as
    // push-to-talk and without touching push-to-talk's state. `done` runs once on
    // `context`'s thread (never, if `context` is destroyed first or the request is
    // aborted). Returns a ticket for abort().
    struct Transcription {
        bool ok = false;
        bool empty = false;      // the engine ran and heard no words
        QString text;
        QString error;           // user-readable when !ok
    };
    quint64 transcribe(const QByteArray &pcm16, QObject *context,
                       std::function<void(const Transcription &)> done);
    void abort(quint64 ticket);

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
    void readyChanged();

private:
    enum class State { Idle, Listening, Transcribing };
    struct Pending {
        QPointer<QObject> context;
        std::function<void(const Transcription &)> done;
    };
    void setState(State s);
    void fail(const QString &message);
    void onAudio(const QByteArray &pcm);
    void ensureWorker();
    void onPrepared(const QString &error);
    void onFinished(quint64 ticket, const QString &text, const QString &error);

    std::unique_ptr<Transcriber> m_transcriber;
    QThread m_thread;
    TranscribeWorker *m_worker = nullptr;
    QHash<quint64, Pending> m_pending;
    quint64 m_nextTicket = 1;
    quint64 m_clipTicket = 0;          // push-to-talk's request in flight
    bool m_ready = false;
    QString m_prepareError;
    QString m_modelFolder;
    std::unique_ptr<AudioCapture> m_capture;
    QTimer m_maxTimer;
    QByteArray m_pcm;
    State m_state = State::Idle;
    double m_level = 0;
    QString m_error;
    bool m_enabled = false;
    int m_maxSeconds = kDefaultMaxSeconds;
};
