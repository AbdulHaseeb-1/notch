#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>

#include "DeepgramProtocol.h"
#include "DeepgramSettings.h"
#include "EchoCanceller.h"
#include "StreamingPlayer.h"
#include "VoiceInput.h"
#include "WsClient.h"

class QNetworkReply;

// A live, hands-free conversation with Buddy through Deepgram's Voice Agent API.
//
// One WebSocket carries everything: the microphone goes up as 16 kHz PCM; the
// agent's voice comes back as 24 kHz PCM and is played as it arrives. Deepgram
// runs speech recognition, the language model (Claude Haiku 5.5 by default,
// or your own endpoint) and the voice, and decides when you have finished a
// sentence; Noch handles the microphone, the speakers, interruptions and the
// settings.
//
// What makes it Buddy and not just a chatbot: the model has a function,
// ask_buddy. When you ask for something to be *done*, it calls ask_buddy and
// this class emits buddyRequested(); the Buddy UI runs the request through the
// full agent (tools, permission prompts, approvals) and calls answerBuddy()
// with the outcome, which the model then says in its own words.
//
// Privacy: the microphone is open, and audio goes to Deepgram, only between
// start() and stop() - never in the background. The state is always visible.
class DeepgramAgent : public QObject
{
    Q_OBJECT
    // The feature switch: voice conversation is available (not started).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // The Deepgram key, listen fields, think choice and speak voice come
    // from the shared DeepgramSettings object (see setDeepgramSettings),
    // not from properties of this class.
    Q_PROPERTY(double eotThreshold READ eotThreshold WRITE setEotThreshold NOTIFY configChanged)
    Q_PROPERTY(bool flux READ flux NOTIFY configChanged)
    // think (the language model): whether the effective model takes a temperature.
    Q_PROPERTY(bool temperatureApplies READ temperatureApplies NOTIFY configChanged)
    Q_PROPERTY(QString prompt READ prompt WRITE setPrompt NOTIFY configChanged)
    Q_PROPERTY(QString defaultPrompt READ defaultPrompt CONSTANT)
    Q_PROPERTY(bool askBuddy READ askBuddy WRITE setAskBuddy NOTIFY configChanged)
    // speak (the voice)
    Q_PROPERTY(double speakSpeed READ speakSpeed WRITE setSpeakSpeed NOTIFY configChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY configChanged)
    Q_PROPERTY(QString greeting READ greeting WRITE setGreeting NOTIFY configChanged)
    // behaviour
    // "headphones": you can talk over the agent. "speakers": the microphone is
    // muted while the agent speaks, so it does not hear (and answer) itself.
    Q_PROPERTY(QString audioMode READ audioMode WRITE setAudioMode NOTIFY configChanged)
    // Speaker mode: take the agent's own voice out of the microphone, so talking over it is heard
    // reliably and the first words of an interruption are not lost.
    Q_PROPERTY(bool echoCancel READ echoCancel WRITE setEchoCancel NOTIFY configChanged)
    // Replies of a sentence or two; anything that needs thought goes to Buddy. Faster on a voice call.
    Q_PROPERTY(bool quickReplies READ quickReplies WRITE setQuickReplies NOTIFY configChanged)
    // How long Buddy keeps listening for a follow-up after it has answered, in seconds; 0 =
    // not at all: the conversation ends once Buddy has said its piece (about five seconds'
    // grace, thirty if it ended on a question) and the wake word starts the next one.
    Q_PROPERTY(int followUpSeconds READ followUpSeconds WRITE setFollowUpSeconds NOTIFY configChanged)
    // Why the configuration cannot be used ("" if it can).
    Q_PROPERTY(QString problem READ problem NOTIFY configChanged)

    // the session
    // "off" | "connecting" | "listening" | "thinking" | "speaking"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY stateChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(double outputLevel READ outputLevel NOTIFY levelChanged)
    Q_PROPERTY(QString userText READ userText NOTIFY transcriptChanged)
    Q_PROPERTY(QString agentText READ agentText NOTIFY transcriptChanged)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY latencyChanged)
    // How the conversation is going, for Settings. Reset when a conversation starts.
    // avgLatencyMs: Deepgram's own report of how long it took to answer (average).
    // responseDelayMs: from your last word to the first sound you heard, here, buffer included
    // (the last one, and the average). stalls: how many times the stream ran dry mid-answer;
    // bufferMs: the jitter buffer in use (it grows after a stall). echoReductionDb: how much of
    // the agent's voice the canceller is taking out of the microphone.
    Q_PROPERTY(int avgLatencyMs READ avgLatencyMs NOTIFY statsChanged)
    Q_PROPERTY(int responseDelayMs READ responseDelayMs NOTIFY statsChanged)
    Q_PROPERTY(int avgResponseDelayMs READ avgResponseDelayMs NOTIFY statsChanged)
    Q_PROPERTY(int replies READ replies NOTIFY statsChanged)
    Q_PROPERTY(int stalls READ stalls NOTIFY statsChanged)
    Q_PROPERTY(int bufferMs READ bufferMs NOTIFY statsChanged)
    Q_PROPERTY(double echoReductionDb READ echoReductionDb NOTIFY statsChanged)
    Q_PROPERTY(bool buddyBusy READ buddyBusy NOTIFY buddyBusyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY errorChanged)
    // changes made during a conversation that apply from the next one
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY configChanged)

    // the connection test and the lists in Settings
    // "" | "testing" | "ok" | "failed"
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged)
    Q_PROPERTY(QString testMessage READ testMessage NOTIFY testChanged)
    Q_PROPERTY(QVariantList thinkModels READ thinkModels NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY modelsChanged)
    Q_PROPERTY(bool modelsLoading READ modelsLoading NOTIFY modelsChanged)
    Q_PROPERTY(QString modelsProblem READ modelsProblem NOTIFY modelsChanged)

public:
    explicit DeepgramAgent(QObject *parent = nullptr);
    ~DeepgramAgent() override;

    // ---- configuration ----------------------------------------------------------------
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    // The one Deepgram configuration, shared with push-to-talk and spoken
    // answers: key, listen fields, think choice and speak voice are read
    // from it (m_cfg is refreshed from it on every change). Until one is
    // attached, a private default is used.
    void setDeepgramSettings(DeepgramSettings *settings);
    DeepgramSettings *deepgramSettings() { return m_settings ? m_settings : &m_fallbackSettings; }
    const DeepgramSettings *deepgramSettings() const { return m_settings ? m_settings : &m_fallbackSettings; }
    double eotThreshold() const { return m_cfg.eotThreshold; }
    void setEotThreshold(double t);
    bool flux() const { return deepgram::isFluxModel(m_cfg.listenModel); }
    bool temperatureApplies() const { return deepgram::modelAcceptsTemperature(m_cfg.thinkProvider, m_cfg.thinkModel); }
    QString prompt() const { return m_cfg.prompt; }
    void setPrompt(const QString &p);
    QString defaultPrompt() const { return deepgram::defaultPrompt(); }
    bool askBuddy() const { return m_cfg.askBuddy; }
    void setAskBuddy(bool on);
    bool quickReplies() const { return m_cfg.quickReplies; }
    void setQuickReplies(bool on);
    bool echoCancel() const { return m_echoCancel; }
    void setEchoCancel(bool on);
    double speakSpeed() const { return m_cfg.speakSpeed; }
    void setSpeakSpeed(double s);
    double volume() const { return m_volume; }
    void setVolume(double v);
    QString greeting() const { return m_cfg.greeting; }
    void setGreeting(const QString &g);
    QString audioMode() const { return m_audioMode; }
    void setAudioMode(const QString &mode);
    int followUpSeconds() const { return m_followUp; }
    void setFollowUpSeconds(int s);
    QString problem() const { return deepgram::configProblem(m_cfg); }
    bool restartNeeded() const { return m_restartNeeded; }
    const deepgram::AgentConfig &config() const { return m_cfg; }

    // Tests: a local server in place of Deepgram, a scripted microphone, no sound.
    void setEndpoint(const QString &url) { m_cfg.endpoint = url; }
    void setApiBase(const QString &base) { m_apiBase = base; }
    void setThinkModelsUrl(const QString &url) { m_thinkModelsUrl = url; }
    void setCapture(std::unique_ptr<AudioCapture> capture);
    StreamingPlayer *player() { return &m_player; }
    void setKeepAliveMs(int ms) { m_keepAlive.setInterval(ms); }
    void setIdleTimeoutMs(int ms) { m_idleOverrideMs = ms; }
    void setGraceMs(int shortMs, int questionMs) { m_graceMs = shortMs; m_graceQuestionMs = questionMs; }

    // ---- the session ------------------------------------------------------------------
    QString state() const;
    bool active() const { return m_state != State::Off; }
    bool speaking() const { return m_state == State::Speaking; }
    bool muted() const { return m_muted; }
    void setMuted(bool on);
    double level() const { return m_level; }
    double outputLevel() const { return m_player.level(); }
    QString userText() const { return m_userText; }
    QString agentText() const { return m_agentText; }
    int latencyMs() const { return m_latencyMs; }
    int avgLatencyMs() const { return m_latencyCount > 0 ? int(m_latencySum / m_latencyCount) : 0; }
    int responseDelayMs() const { return m_responseMs; }
    int avgResponseDelayMs() const { return m_responseCount > 0 ? int(m_responseSum / m_responseCount) : 0; }
    int replies() const { return m_replies; }
    int stalls() const { return m_stalls; }
    int bufferMs() const { return m_player.prebufferMs(); }
    double echoReductionDb() const { return m_aec.erleDb(); }
    bool buddyBusy() const { return !m_calls.isEmpty(); }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }

    // Opens the microphone and the conversation. False, with `error` set, if it cannot.
    Q_INVOKABLE bool start();
    // Ends it: closes the microphone and the connection and stops any speech.
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    // Cuts the agent off mid-sentence (for speaker mode, where the microphone is muted while it talks).
    Q_INVOKABLE void interrupt();
    // Keeps the microphone shut (speaker mode) for this long: the app is about to make a sound
    // of its own, and the agent must not hear it.
    Q_INVOKABLE void holdMic(int ms);
    // The agent says exactly this (an approval request, a question). Queued after what it is saying.
    Q_INVOKABLE void say(const QString &text);
    // Types to the agent once the conversation is ready (right away if it already is): what
    // followed the wake phrase ("hey buddy, open notepad") while the call was still being made.
    Q_INVOKABLE void queueText(const QString &text);
    // Types to the agent as if it had been spoken.
    Q_INVOKABLE void sendText(const QString &text);
    // The outcome of a task (or an answer) the agent handed to Buddy.
    Q_INVOKABLE void answerBuddy(const QString &callId, const QString &outcome);
    Q_INVOKABLE void clearError();

    // Checks the key, the model and the voice end to end without the microphone: connects, applies the settings, hangs up.
    Q_INVOKABLE void test();
    // Reloads the lists of language models and voices from Deepgram.
    Q_INVOKABLE void refreshModels();
    QString testState() const { return m_testState; }
    QString testMessage() const { return m_testMessage; }
    QVariantList thinkModels() const { return m_thinkModels; }
    QVariantList voices() const { return m_voices; }
    bool modelsLoading() const { return m_modelsPending > 0; }
    QString modelsProblem() const { return m_modelsProblem; }

signals:
    void configChanged();
    void stateChanged();
    void mutedChanged();
    void levelChanged();
    void transcriptChanged();
    void latencyChanged();
    void statsChanged();
    void buddyBusyChanged();
    void errorChanged();
    void testChanged();
    void modelsChanged();
    // The user said this / the agent said this (final text of a turn).
    void userSaid(const QString &text);
    void agentSaid(const QString &text);
    // The agent wants Buddy to do this. Answer with answerBuddy(callId, ...).
    void buddyRequested(const QString &callId, const QString &request);
    // The agent relays what the user said to a question or approval Buddy asked.
    // Answer with answerBuddy(callId, ...) once the run it unblocked has an outcome.
    void buddyAnswered(const QString &callId, const QString &answer);
    // The call was cancelled (the user spoke over it); drop the answer.
    void buddyCancelled(const QString &callId);
    // A conversation started / ended (for the sound cues and the wake word).
    void started();
    void ended();

private:
    enum class State { Off, Connecting, Listening, Thinking, Speaking };
    struct Call { QString name; QTimer *timeout = nullptr; };

    void setState(State s);
    void fail(const QString &message);
    void setNotice(const QString &message);
    void finishSession(bool reconnect);
    void openSocket(bool resume);
    void onOpened();
    void onText(const QString &json);
    void onBinary(const QByteArray &pcm);
    void onClosed(quint16 code, const QString &reason, bool clean);
    void handleEvent(const deepgram::Event &e);
    void startMicrophone();
    void onMicAudio(const QByteArray &pcm);
    void onAgentDrained();
    void sendLiveChanges();
    void touchActivity();
    void armFollowUp();
    void respondToCall(const QString &id, const QString &outcome);
    void dropCalls();
    void setConfigChanged();
    bool gateClosed() const;
    bool echoCancelling() const;
    bool userTalkingOver(bool loud);
    void processMicFrame(QByteArray frame);
    void sendMicFrame(const QByteArray &frame);
    void sendPreroll();
    void resetStats();
    void noteFirstSound();
    void remember(const QString &role, const QString &text);

    // connection test
    void finishTest(bool ok, const QString &message);
    // Copies the shared Deepgram fields into m_cfg; emits configChanged
    // (and live-updates a running conversation) when anything moved.
    void syncFromSettings();

    deepgram::AgentConfig m_cfg;
    DeepgramSettings *m_settings = nullptr;
    DeepgramSettings m_fallbackSettings;
    bool m_enabled = false;
    QString m_audioMode = QStringLiteral("speakers");
    int m_followUp = 30;
    int m_graceMs = 5000;            // wake-word-only mode: how long after its reply it waits
    int m_graceQuestionMs = 30000;   // ... when the reply ended on a question
    int m_idleOverrideMs = 0;       // tests
    double m_volume = 1.0;

    WsClient m_ws;
    StreamingPlayer m_player;
    std::unique_ptr<AudioCapture> m_capture;
    QTimer m_keepAlive;
    QTimer m_idle;
    QTimer m_reconnect;
    QTimer m_ackTimer;               // Buddy is working and the line is quiet: say so now and then
    int m_ackCount = 0;
    qint64 m_lastAudioMs = 0;        // when the agent last made a sound
    State m_state = State::Off;
    bool m_settingsApplied = false;
    bool m_resuming = false;
    int m_reconnects = 0;
    bool m_stopping = false;
    bool m_muted = false;
    bool m_agentAudioDone = true;
    bool m_dropAgentAudio = false;
    bool m_userSpeaking = false;     // the server reported speech start; no final transcript yet
    qint64 m_dropUntilMs = 0;        // agent audio before this moment is stale (a barge-in just happened)
    qint64 m_gateOpensAtMs = 0;      // speaker mode: the microphone stays muted until this moment
    bool m_everApplied = false;
    QByteArray m_oddByte;
    QStringList m_queuedText;
    bool m_lastReplyAsked = false;   // the agent's last words ended on a question            // half a sample left over from the last audio message
    int m_bargeFrames = 0;           // consecutive loud microphone frames while the agent talks (speaker mode)
    bool m_echoCancel = true;
    EchoCanceller m_aec;
    // The last few microphone frames, with whether each was clearly the user's voice. When the
    // gate opens (the user talks over the agent) the first words are already in here.
    struct MicFrame { QByteArray pcm; bool loud = false; };
    QList<MicFrame> m_preroll;
    bool m_wasEchoGated = false;     // the previous frame was held back because the agent was talking
    int m_frames = 0;
    double m_reportedErle = 0.0;
    // stats
    qint64 m_turnEndMs = 0;          // when the user's last words were final (0 = nothing waiting for an answer)
    int m_responseMs = 0;
    qint64 m_responseSum = 0;
    int m_responseCount = 0;
    qint64 m_latencySum = 0;
    int m_latencyCount = 0;
    int m_replies = 0;
    int m_stalls = 0;
    QString m_lastWsError;
    QByteArray m_micBuffer;
    double m_level = 0.0;
    QString m_userText, m_agentText;
    QString m_error, m_notice;
    int m_latencyMs = 0;
    QHash<QString, Call> m_calls;
    QList<QPair<QString, QString>> m_history;
    deepgram::AgentConfig m_sent;     // what the running session was started with
    bool m_restartNeeded = false;
    QString m_apiBase = QString::fromLatin1(deepgram::kApiBase);
    QString m_thinkModelsUrl = QString::fromLatin1(deepgram::kThinkModelsUrl);

    // connection test
    WsClient m_testWs;
    QTimer m_testTimeout;
    QString m_testState, m_testMessage, m_testWarning;
    bool m_testApplied = false;
    QElapsedTimer m_testClock;
    QMetaObject::Connection m_testErrConn;

    // the lists
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_thinkReply, m_voicesReply;
    int m_modelsPending = 0;
    QString m_modelsProblem;
    QVariantList m_thinkModels, m_voices;
};
