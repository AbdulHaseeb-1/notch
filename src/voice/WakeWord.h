#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <memory>

#include "VoiceInput.h"

// Wake word for Buddy: say the phrase (default "hey buddy"), optionally followed
// by what you want ("hey buddy, open notepad"), and Buddy listens and does it.
//
// How it works, and what that means for privacy (this is an open microphone):
//  * off until the user turns it on, and it needs Voice input set up first;
//  * while on, the microphone is open and a small local detector cuts the sound
//    into utterances; silence and noise never leave the computer;
//  * every utterance is sent to the speech-to-text service to check for the wake
//    phrase, because there is no local keyword model. So with a service on the
//    internet it sends EVERYTHING said near the microphone there; it therefore
//    refuses to run against a non-local service until the user has explicitly
//    allowed that (allowCloud). A speech service on this computer needs no consent;
//  * what is heard is only compared with the phrase and then dropped: it is not
//    stored, logged or shown unless the phrase was said at the start of it;
//  * an utterance is only a wake-up when the phrase is at its very start
//    (optionally after "hey"/"ok"), so the word in passing conversation does nothing;
//  * the number of utterances checked per minute is capped, and a failing service
//    closes the microphone and backs off instead of hammering it;
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
    // Sending everything heard to a speech service on the internet needs a yes.
    Q_PROPERTY(bool allowCloud READ allowCloud WRITE setAllowCloud NOTIFY configChanged)
    // "" when it can run (or is off), else why it cannot.
    Q_PROPERTY(QString blocked READ blockedReason NOTIFY configChanged)
    // "off" | "blocked" | "paused" | "listening" | "awaiting" (the phrase was heard; what next?)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // True whenever the microphone is open for this: show it, always.
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kMaxChecksPerMinute = 12;
    static constexpr int kCooldownMs = 1500;
    static constexpr int kAwaitStartMs = 7000;     // how long to wait for the command after the phrase alone
    static constexpr int kMaxCommandChars = 2000;
    static constexpr int kBackoffBaseSec = 10;     // doubles per failure, up to kBackoffMaxSec
    static constexpr int kBackoffMaxSec = 300;

    explicit WakeWord(VoiceInput *voice, QObject *parent = nullptr);
    ~WakeWord() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString phrase() const { return m_phrase; }
    void setPhrase(const QString &phrase);
    bool allowCloud() const { return m_allowCloud; }
    void setAllowCloud(bool on);
    Q_INVOKABLE QString blockedReason() const;

    QString state() const;
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
    void setBackoffBaseSeconds(int s) { m_backoffBaseSec = qMax(1, s); }
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

private:
    enum class Mode { Listening, Awaiting };
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
    bool underRateLimit();

    VoiceInput *m_voice;
    std::unique_ptr<AudioCapture> m_capture;
    wake::Segmenter m_segmenter;
    wake::SegmenterConfig m_listenConfig;
    QTimer m_awaitTimer;
    QTimer m_backoffTimer;
    QPointer<QNetworkReply> m_reply;
    QList<qint64> m_checkTimes;
    bool m_enabled = false;
    bool m_allowCloud = false;
    bool m_paused = false;
    bool m_micOpen = false;
    Mode m_mode = Mode::Listening;
    QString m_phrase = QStringLiteral("hey buddy");
    QString m_error;
    quint64 m_generation = 0;
    qint64 m_lastCommandMs = 0;
    int m_awaitStartMs = kAwaitStartMs;
    int m_cooldownMs = kCooldownMs;
    int m_backoffBaseSec = kBackoffBaseSec;
    int m_failures = 0;
    bool m_backingOff = false;
    bool m_halted = false;          // stopped until the settings change (refused key, no microphone)
    QString m_lastState;
    int m_dropped = 0;
    int m_checked = 0;
};
