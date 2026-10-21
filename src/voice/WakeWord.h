#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <atomic>

#include <memory>

#include "VoiceInput.h"

class WakeWorker;

// Wake word for Buddy: say the phrase (default "hey buddy"), optionally followed
// by what you want ("hey buddy, open notepad"), and Buddy listens and does it.
//
// How it works, and what that means for privacy (this is an open microphone):
//  * off until the user turns it on, and it needs Voice input set up first;
//  * the phrase is found on this computer: an openWakeWord model (WakeDetector,
//    "Hey Buddy" ships with Noch) listens to the microphone through ONNX
//    Runtime. Nothing is sent anywhere, ever: once the phrase is heard, the request
//    that follows is transcribed on this computer too (Moonshine Tiny);
//  * without ONNX Runtime or the wake model it falls back to the older method:
//    the sound is cut into utterances locally and each one is transcribed (also
//    on this computer) and compared with the phrase;
//  * what is heard is only compared with the phrase and then dropped: it is not
//    stored, logged or shown unless the phrase was said at the start of it;
//  * an utterance is only a wake-up when the phrase is at its very start
//    (optionally after "hey"/"ok"), so the word in passing conversation does nothing;
//  * a transcriber that fails closes the microphone until the settings change,
//    instead of retrying forever;
//  * it pauses while push-to-talk is in use and while Buddy speaks;
//  * what it hears becomes an ordinary request: every tool still goes through the
//    permission gate and its approvals.
namespace wake {

// ---- phrase matching (pure) ---------------------------------------------------------

// Lower case, letters and digits only, accents removed ("Búddy!" -> "buddy").
QString normalizeWord(const QString &word);

// Why a phrase cannot be a wake phrase ("" if it can): 4 to 40 letters in
// 1 to 4 words.
QString phraseProblem(const QString &phrase);

struct Match {
    bool found = false;
    QString rest;          // what follows the phrase, as the user said it ("" if nothing)
};
// Does `transcript` start with `phrase`? Tolerant of how a speech-to-text
// service writes it: case, punctuation, "heybuddy", and one wrong letter in a
// longer word ("buddie"). Only the first word may be a filler ("hey", "ok", ...)
// ahead of the phrase.
Match match(const QString &transcript, const QString &phrase);

// A reply as it should be read aloud: no code, links or Markdown, at most
// `maxChars`, ending on a sentence where it can.
QString spokenSummary(const QString &text, int maxChars = 280);

// Sustain-voted wake decision. A single hot 80 ms chunk can be TV noise or a
// phoneme coincidence, so the phrase only counts when the score stays at or
// above the threshold for `needed` consecutive score callbacks (about 80 ms
// of latency per extra vote). Returns true exactly once when the streak
// reaches `needed`, and consumes the streak.
bool confirmSustained(int &streak, float score, float threshold, int needed);

// ---- finding utterances in a stream of audio (pure) ----------------------------------

struct SegmenterConfig {
    int frameMs = 20;
    double minLevel = 0.03;       // never count anything quieter than this as speech
    double floorFactor = 3.0;     // speech must be this many times louder than the room
    int startMs = 120;            // sound this long opens an utterance
    int endSilenceMs = 700;       // quiet this long closes it
    int maxMs = 8000;             // an utterance is cut here
    int preRollMs = 300;          // kept from before the sound began, so the first word is whole
    int minSpeechMs = 300;        // shorter than this is a click, not a word
};

// Feed it 16 kHz mono 16-bit audio in any chunk size; it hands back finished utterances.
class Segmenter
{
public:
    explicit Segmenter(const SegmenterConfig &config = SegmenterConfig());
    void setConfig(const SegmenterConfig &config);
    const SegmenterConfig &config() const { return m_config; }
    void reset();

    // Finished utterances land in `out`. Returns true when an utterance began during this call.
    bool feed(const QByteArray &pcm, QList<QByteArray> *out);
    bool inUtterance() const { return m_in; }
    double noiseFloor() const { return m_floor; }

private:
    void processFrame(const QByteArray &frame, QList<QByteArray> *out, bool *started);
    void finish(QList<QByteArray> *out);

    SegmenterConfig m_config;
    QByteArray m_pending;               // not yet a whole frame
    QList<QByteArray> m_preRoll;
    QByteArray m_segment;
    bool m_in = false;
    int m_speechRun = 0;
    int m_speechFrames = 0;
    int m_silentRun = 0;
    int m_frames = 0;
    double m_floor = 0.01;
};

} // namespace wake

class WakeWord : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    Q_PROPERTY(QString phrase READ phrase WRITE setPhrase NOTIFY configChanged)
    // The request that follows the phrase is taken by something else (a live conversation
    // starts on woke()): the wake word then does not wait for one, and needs no speech to
    // text of its own, only the on-device detector.
    Q_PROPERTY(bool handsOff READ handsOff WRITE setHandsOff NOTIFY configChanged)
    // "" when it can run (or is off), else why it cannot.
    Q_PROPERTY(QString blocked READ blockedReason NOTIFY configChanged)
    // "off" | "blocked" | "loading" | "paused" | "listening" | "awaiting" (the phrase was heard; what next?)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // How the phrase is found: "local" (openWakeWord model), "loading" (still starting
    // it) or "speech" (each sentence is transcribed and compared with the phrase).
    Q_PROPERTY(QString detector READ detector NOTIFY detectorChanged)
    // Why the local detector is not in use ("" when it is, or while it starts).
    Q_PROPERTY(QString localProblem READ localProblem NOTIFY detectorChanged)
    // Where models can be dropped in to replace the bundled "Hey Buddy".
    Q_PROPERTY(QString modelFolder READ modelFolder CONSTANT)
    // How sure the model has to be, 0.2 (easily woken) .. 0.9 (strict). 0.5 is the model's own default.
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY configChanged)
    // True whenever the microphone is open for this: show it, always.
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kCooldownMs = 1500;
    static constexpr int kAwaitStartMs = 7000;     // how long to wait for the command after the phrase alone
    static constexpr int kMaxCommandChars = 2000;
    // Two consecutive above-threshold score callbacks (~160 ms) before the
    // phrase counts: one hot 80 ms chunk is too often TV noise.
    static constexpr int kWakeStreakNeeded = 2;

    explicit WakeWord(VoiceInput *voice, QObject *parent = nullptr);
    ~WakeWord() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool handsOff() const { return m_handsOff; }
    void setHandsOff(bool on);
    QString phrase() const { return m_phrase; }
    void setPhrase(const QString &phrase);
    Q_INVOKABLE QString blockedReason() const;

    QString state() const;
    QString detector() const;
    QString localProblem() const { return m_localError; }
    QString modelFolder() const { return m_modelFolder; }
    double threshold() const { return m_threshold; }
    void setThreshold(double t);
    // Tests point this at a folder of models (the shipped ones are read from the resources).
    void setModelFolder(const QString &folder);
    Q_INVOKABLE void openModelFolder() const;
    // What the settings shows about the wake models: the files and their state.
    Q_INVOKABLE QVariantMap detectorModelStatus() const;
    bool active() const { return m_micOpen; }
    QString error() const { return m_error; }

    void setCapture(std::unique_ptr<AudioCapture> capture);
    // Buddy is speaking (or otherwise busy): stop listening until released.
    Q_INVOKABLE void setPaused(bool paused);
    // Drop whatever was half-heard and go back to waiting for the phrase.
    Q_INVOKABLE void cancelAwaiting();
    Q_INVOKABLE void clearError();

    // Tests shorten these.
    void setSegmenterConfig(const wake::SegmenterConfig &config) { m_listenConfig = config; m_segmenter.setConfig(config); }
    void setAwaitStartMs(int ms) { m_awaitStartMs = qMax(10, ms); }
    void setCooldownMs(int ms) { m_cooldownMs = qMax(0, ms); }
    int droppedUtterances() const { return m_dropped; }
    int checkedUtterances() const { return m_checked; }

signals:
    // The phrase was heard. Come to attention (a sound, a look).
    void woke();
    // What to do, in the user's words. Comes right after woke() when the phrase
    // was followed by a request, or after the next utterance when it was not.
    void command(const QString &text);
    // The phrase was said alone and nothing followed it in time.
    void timedOut();
    void failed(const QString &message);
    void stateChanged();
    void configChanged();
    void errorChanged();
    void detectorChanged();

private:
    enum class Mode { Listening, Awaiting };
    enum class Local { Untried, Loading, Ready, Failed };

    // The local detector (WakeDetector on a worker thread).
    bool localWanted() const;          // models are there to try
    bool usingLocal() const;           // local, and not known to be broken
    void startLocal();
    void stopLocal();
    void onWorkerReady(bool ok, const QString &error);
    void onWorkerScore(float best, quint64 generation);
    void feedLocal(const QByteArray &pcm);
    void resetLocalDetector();
    void setLocalState(Local state, const QString &error = QString());

    void refresh();
    bool shouldListen() const;
    bool openMic();
    void closeMic();
    void onAudio(const QByteArray &pcm);
    void onUtterance(const QByteArray &pcm);
    void check(const QByteArray &pcm, Mode mode);
    void onTranscript(const VoiceInput::Transcription &t, Mode mode);
    void failure(const VoiceInput::Transcription &t, Mode mode);
    void deliver(const QString &text);
    void enterAwaiting();
    void leaveAwaiting();
    void fail(const QString &message);

    VoiceInput *m_voice;
    std::unique_ptr<AudioCapture> m_capture;
    wake::Segmenter m_segmenter;
    wake::SegmenterConfig m_listenConfig;
    QTimer m_awaitTimer;
    quint64 m_ticket = 0;           // the transcription in flight (0: none)
    bool m_enabled = false;
    bool m_paused = false;
    bool m_micOpen = false;
    Mode m_mode = Mode::Listening;
    QString m_phrase = QStringLiteral("hey buddy");
    QString m_error;
    quint64 m_generation = 0;
    qint64 m_lastCommandMs = 0;
    int m_awaitStartMs = kAwaitStartMs;
    int m_cooldownMs = kCooldownMs;
    bool m_halted = false;          // stopped until the settings change (the model will not load, no microphone)
    QString m_lastState;

    QString m_modelFolder;
    double m_threshold = 0.5;
    Local m_local = Local::Untried;
    QString m_localError;
    QThread m_workerThread;
    WakeWorker *m_worker = nullptr;
    QByteArray m_localBuf;          // microphone audio waiting for a whole 80 ms step
    quint64 m_detGeneration = 0;    // scores from before the last reset are stale
    std::atomic<int> m_inflight{0}; // audio batches handed to the worker and not scored yet
    qint64 m_lastWakeMs = 0;
    int m_wakeStreak = 0;      // consecutive above-threshold score callbacks
    bool m_handsOff = false;
    mutable int m_localWantedCache = -1;   // -1: not looked yet
    int m_dropped = 0;
    int m_checked = 0;
};
