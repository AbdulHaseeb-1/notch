#include "DeepgramAgent.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <algorithm>

#include "VoiceAudio.h"

#include <QLoggingCategory>

// Off by default. QT_LOGGING_RULES="noch.deepgram.debug=true" prints the conversation's
// message types, transitions and counters (never audio, keys or settings).
Q_LOGGING_CATEGORY(lcDeepgram, "noch.deepgram", QtWarningMsg)

namespace {

constexpr int kFrameBytes = 1280;                 // 40 ms of 16 kHz mono 16-bit
constexpr qint64 kMaxBacklogBytes = 512 * 1024;   // a stalled network drops audio instead of queueing it
constexpr int kBargeInDropMs = 150;               // stale agent audio still in flight after a barge-in
constexpr int kGateTailMs = 150;                  // the room still rings a moment after the speakers stop
// Speaker mode barge-in: the microphone hears the agent's own voice, so a voice counts as the
// user talking over it only when it is clearly louder than that echo, for a few frames in a row.
constexpr double kBargeMinLevel = 0.12;          // quieter than this is never a voice over the agent
constexpr double kBargeEchoRatio = 0.6;          // the echo at the microphone is about this share of the output
constexpr double kBargeEchoRatioAec = 0.25;      // ... once the echo canceller is working
constexpr int kPrerollFrames = 8;                // 320 ms of microphone kept for the start of an interruption
constexpr double kBargeEchoMargin = 0.08;        // ... plus a margin for the room
constexpr int kBargeFrames = 4;                  // 4 x 40 ms of loud voice
constexpr int kAckFirstMs = 2200;                 // a handed-off task this long with nothing said: say "one moment"
constexpr int kAckAgainMs = 6000;
constexpr int kAckMax = 3;
constexpr int kBuddyTimeoutMs = 180000;           // the longest a handed-off task may take
constexpr int kMaxReconnects = 3;
constexpr int kTestTimeoutMs = 20000;
constexpr int kMaxAnswerChars = 1600;

QString clamp1(const QString &s) { return s.trimmed(); }

} // namespace

DeepgramAgent::DeepgramAgent(QObject *parent)
    : QObject(parent)
{
    // The fallback settings object is the live one until a shared one is
    // attached, so edits to it refresh m_cfg just the same.
    connect(&m_fallbackSettings, &DeepgramSettings::configChanged, this, [this] { syncFromSettings(); });
    syncFromSettings();
    m_keepAlive.setInterval(deepgram::kKeepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] {
        if (m_ws.isOpen())
            m_ws.sendText(deepgram::keepAliveMessage());
    });
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (!active())
            return;
        if (!m_calls.isEmpty()) {          // still busy for the user: not idle
            touchActivity();
            return;
        }
        if (m_state == State::Speaking) {   // it is mid-answer: not quiet
            touchActivity();
            return;
        }
        if (m_followUp > 0)
            setNotice(tr("Ended the conversation after %1 seconds of quiet.").arg(m_followUp));
        stop();
    });
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this] {
        if (active())
            openSocket(true);
    });
    m_ackTimer.setSingleShot(true);
    connect(&m_ackTimer, &QTimer::timeout, this, [this] {
        // Buddy is still working and nothing has been said for a while: a quiet line feels like a hang.
        if (!active() || m_calls.isEmpty() || m_ackCount >= kAckMax)
            return;
        const qint64 quiet = QDateTime::currentMSecsSinceEpoch() - m_lastAudioMs;
        if (m_state == State::Speaking || !m_player.idle() || quiet < kAckFirstMs) {
            m_ackTimer.start(500);          // it is talking: look again when it has finished
            return;
        }
        say(m_ackCount == 0 ? tr("One moment.")
            : m_ackCount == 1 ? tr("Still working on it.")
                              : tr("This one is taking a little longer."));
        ++m_ackCount;
        m_ackTimer.start(kAckAgainMs);
    });
    m_testTimeout.setSingleShot(true);
    connect(&m_testTimeout, &QTimer::timeout, this, [this] {
        finishTest(false, tr("Deepgram did not answer in time."));
    });

    connect(&m_ws, &WsClient::opened, this, &DeepgramAgent::onOpened);
    connect(&m_ws, &WsClient::textReceived, this, &DeepgramAgent::onText);
    connect(&m_ws, &WsClient::binaryReceived, this, &DeepgramAgent::onBinary);
    connect(&m_ws, &WsClient::closed, this, &DeepgramAgent::onClosed);
    connect(&m_ws, &WsClient::errorOccurred, this, [this](const QString &why) { m_lastWsError = why; });

    connect(&m_player, &StreamingPlayer::drained, this, &DeepgramAgent::onAgentDrained);
    connect(&m_player, &StreamingPlayer::levelChanged, this, &DeepgramAgent::levelChanged);
    connect(&m_player, &StreamingPlayer::failed, this, [this](const QString &why) { setNotice(why); });
    // What goes to the speakers is what the echo canceller must take back out of the microphone.
    connect(&m_player, &StreamingPlayer::reference, this, [this](const QByteArray &pcm) {
        if (echoCancelling())
            m_aec.pushReference(pcm);
    });
    connect(&m_player, &StreamingPlayer::referenceReset, this, [this] { m_aec.clearReference(); });
    connect(&m_player, &StreamingPlayer::playbackStarted, this, &DeepgramAgent::noteFirstSound);
    connect(&m_player, &StreamingPlayer::underrun, this, [this](int bufferMs) {
        ++m_stalls;
        qInfo().noquote() << "noch voice: the stream stalled mid-answer; jitter buffer now" << bufferMs
                          << "ms (stall" << m_stalls << "this conversation)";
        emit statsChanged();
    });
    m_player.setInputRate(deepgram::kOutputRate);
    m_voices = deepgram::builtinVoices();

    // The connection test uses its own socket so it can run beside nothing else, never beside a conversation.
    connect(&m_testWs, &WsClient::textReceived, this, [this](const QString &json) {
        const deepgram::Event e = deepgram::parseEvent(json);
        switch (e.type) {
        case deepgram::Event::Welcome: {
            deepgram::AgentConfig c = m_cfg;
            c.greeting.clear();
            c.history.clear();
            m_testWs.sendText(QString::fromUtf8(QJsonDocument(deepgram::buildSettings(c)).toJson(QJsonDocument::Compact)));
            break;
        }
        case deepgram::Event::SettingsApplied:
            m_testApplied = true;
            m_testClock.start();
            // The settings parse is not the whole story: a model that rejects a field only says
            // so when it is first asked something. Ask it, and wait for the answer.
            m_testWs.sendText(deepgram::injectUserMessage(QStringLiteral("Reply with the single word: ready.")));
            break;
        case deepgram::Event::ConversationText:
            if (e.role == QLatin1String("assistant") && !e.text.trimmed().isEmpty())
                finishTest(true, tr("Connected. %1 answered in %2 ms.").arg(m_cfg.thinkModel).arg(m_testClock.elapsed()));
            break;
        case deepgram::Event::Error:
            finishTest(false, deepgram::friendlyProblem(e.code, e.description).isEmpty() ? e.description
                                                                                         : deepgram::friendlyProblem(e.code, e.description));
            break;
        case deepgram::Event::Warning:
            if (e.code == QLatin1String("THINK_REQUEST_FAILED"))
                m_testWarning = deepgram::friendlyProblem(e.code, e.description);
            break;
        default:
            break;
        }
    });
    connect(&m_testWs, &WsClient::closed, this, [this](quint16, const QString &reason, bool) {
        if (m_testState == QLatin1String("testing"))
            finishTest(false, m_testWarning.isEmpty() ? (reason.isEmpty() ? tr("Deepgram closed the connection.") : reason)
                                                      : m_testWarning);
    });
}

DeepgramAgent::~DeepgramAgent()
{
    m_stopping = true;
    m_ws.abort();
    m_testWs.abort();
    if (m_capture)
        m_capture->stop();
    m_player.flush();
}

// ---- configuration ----------------------------------------------------------------------------

void DeepgramAgent::setConfigChanged()
{
    emit configChanged();
    if (active())
        sendLiveChanges();
}

void DeepgramAgent::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        stop();
    emit configChanged();
}

#define NOCH_DG_STRING(Setter, member)                  \
    void DeepgramAgent::Setter(const QString &value)    \
    {                                                   \
        const QString v = clamp1(value);                \
        if (v == m_cfg.member)                          \
            return;                                     \
        m_cfg.member = v;                               \
        setConfigChanged();                             \
    }
NOCH_DG_STRING(setGreeting, greeting)
#undef NOCH_DG_STRING

// The shared Deepgram fields (key, listen, think, speak voice) live in the
// settings object; this class keeps a working copy in m_cfg and refreshes
// it whenever the object changes, so a running conversation live-updates
// what it can and flags the rest as needing a restart, exactly as before.
void DeepgramAgent::setDeepgramSettings(DeepgramSettings *settings)
{
    // Drop both possible live connections (the old effective object and the
    // fallback the constructor wired) so the new one is connected exactly once.
    disconnect(deepgramSettings(), &DeepgramSettings::configChanged, this, nullptr);
    m_fallbackSettings.disconnect(this);
    m_settings = settings;
    connect(deepgramSettings(), &DeepgramSettings::configChanged, this, [this] { syncFromSettings(); });
    syncFromSettings();
}

void DeepgramAgent::syncFromSettings()
{
    const deepgram::AgentConfig before = m_cfg;
    deepgramSettings()->applyToAgentConfig(m_cfg);
    if (before.apiKey != m_cfg.apiKey || before.listenModel != m_cfg.listenModel
        || before.listenLanguage != m_cfg.listenLanguage || before.keyterms != m_cfg.keyterms
        || before.thinkProvider != m_cfg.thinkProvider || before.thinkModel != m_cfg.thinkModel
        || !qFuzzyCompare(before.temperature + 2.0, m_cfg.temperature + 2.0)
        || before.endpointUrl != m_cfg.endpointUrl || before.endpointKey != m_cfg.endpointKey
        || before.speakModel != m_cfg.speakModel)
        setConfigChanged();
}

void DeepgramAgent::setPrompt(const QString &p)
{
    // A prompt equal to the built-in one is stored as "use the default", so a
    // later change to the default reaches users who never edited it.
    const QString v = p.trimmed() == deepgram::defaultPrompt().trimmed() ? QString() : p.trimmed();
    if (v == m_cfg.prompt)
        return;
    m_cfg.prompt = v;
    setConfigChanged();
}

void DeepgramAgent::setEotThreshold(double t)
{
    const double v = std::clamp(t, 0.5, 1.0);
    if (qFuzzyCompare(v, m_cfg.eotThreshold))
        return;
    m_cfg.eotThreshold = v;
    setConfigChanged();
}

void DeepgramAgent::setAskBuddy(bool on)
{
    if (on == m_cfg.askBuddy)
        return;
    m_cfg.askBuddy = on;
    setConfigChanged();
}

void DeepgramAgent::setQuickReplies(bool on)
{
    if (on == m_cfg.quickReplies)
        return;
    m_cfg.quickReplies = on;
    setConfigChanged();
}

void DeepgramAgent::setEchoCancel(bool on)
{
    if (on == m_echoCancel)
        return;
    m_echoCancel = on;
    m_aec.reset();
    m_preroll.clear();
    emit configChanged();
}

void DeepgramAgent::setSpeakSpeed(double s)
{
    const double v = std::clamp(s, 0.5, 1.5);
    if (qFuzzyCompare(v, m_cfg.speakSpeed))
        return;
    m_cfg.speakSpeed = v;
    setConfigChanged();
}

void DeepgramAgent::setVolume(double v)
{
    const double x = std::clamp(v, 0.0, 1.5);
    if (qFuzzyCompare(x + 1.0, m_volume + 1.0))
        return;
    m_volume = x;
    m_player.setVolume(x);
    emit configChanged();
}

void DeepgramAgent::setAudioMode(const QString &mode)
{
    const QString m = mode == QLatin1String("headphones") ? mode : QStringLiteral("speakers");
    if (m == m_audioMode)
        return;
    m_audioMode = m;
    emit configChanged();
}

void DeepgramAgent::setFollowUpSeconds(int s)
{
    const int v = s <= 0 ? 0 : std::clamp(s, 10, 3600);
    if (v == m_followUp)
        return;
    m_followUp = v;
    if (active())
        touchActivity();
    emit configChanged();
}

void DeepgramAgent::setCapture(std::unique_ptr<AudioCapture> capture)
{
    if (m_capture)
        m_capture->stop();
    m_capture = std::move(capture);
}

// A change made during a conversation: the prompt and the voice can be swapped in
// place, everything else needs a new session.
void DeepgramAgent::sendLiveChanges()
{
    if (!m_settingsApplied || !m_ws.isOpen()) {
        return;
    }
    const QString prompt = deepgram::effectivePrompt(m_cfg);
    const QString sentPrompt = deepgram::effectivePrompt(m_sent);
    if (prompt != sentPrompt) {
        m_ws.sendText(deepgram::updatePromptMessage(prompt));
        m_sent.prompt = m_cfg.prompt;
        m_sent.quickReplies = m_cfg.quickReplies;
    }
    if (m_cfg.speakModel != m_sent.speakModel || !qFuzzyCompare(m_cfg.speakSpeed, m_sent.speakSpeed)) {
        m_ws.sendText(deepgram::updateSpeakMessage(m_cfg.speakModel, m_cfg.speakSpeed));
        m_sent.speakModel = m_cfg.speakModel;
        m_sent.speakSpeed = m_cfg.speakSpeed;
    }
    deepgram::AgentConfig a = m_cfg, b = m_sent;
    a.history.clear();
    b.history.clear();
    a.greeting = b.greeting;
    const bool need = deepgram::buildSettings(a) != deepgram::buildSettings(b);
    if (need != m_restartNeeded) {
        m_restartNeeded = need;
        emit configChanged();
    }
}

// ---- state ------------------------------------------------------------------------------------

QString DeepgramAgent::state() const
{
    switch (m_state) {
    case State::Connecting: return QStringLiteral("connecting");
    case State::Listening: return QStringLiteral("listening");
    case State::Thinking: return QStringLiteral("thinking");
    case State::Speaking: return QStringLiteral("speaking");
    case State::Off: break;
    }
    return QStringLiteral("off");
}

void DeepgramAgent::setState(State s)
{
    if (s == m_state)
        return;
    qCDebug(lcDeepgram) << "state" << int(m_state) << "->" << int(s);
    m_state = s;
    emit stateChanged();
}

void DeepgramAgent::setMuted(bool on)
{
    if (on == m_muted)
        return;
    m_muted = on;
    if (on && m_level != 0.0) {
        m_level = 0.0;
        emit levelChanged();
    }
    emit mutedChanged();
}

void DeepgramAgent::clearError()
{
    if (m_error.isEmpty() && m_notice.isEmpty())
        return;
    m_error.clear();
    m_notice.clear();
    emit errorChanged();
}

void DeepgramAgent::fail(const QString &message)
{
    m_error = message;
    m_notice.clear();
    emit errorChanged();
    finishSession(false);
}

void DeepgramAgent::setNotice(const QString &message)
{
    if (message == m_notice)
        return;
    m_notice = message;
    emit errorChanged();
}

void DeepgramAgent::touchActivity()
{
    if (!active())
        return;
    if (m_idleOverrideMs > 0)
        m_idle.start(m_idleOverrideMs);
    else if (m_followUp > 0)
        m_idle.start(m_followUp * 1000);
    else
        m_idle.start(60000);     // wake-word-only mode: a minute to say the first words; the grace after a reply is shorter
}

// The agent has said its piece. In wake-word-only mode the conversation ends shortly after,
// unless there is more to wait for: a task running, or a question put to the user.
void DeepgramAgent::armFollowUp()
{
    if (m_followUp > 0 || !m_calls.isEmpty() || !active())
        return;
    m_idle.start(m_idleOverrideMs > 0 ? m_idleOverrideMs : (m_lastReplyAsked ? m_graceQuestionMs : m_graceMs));
}

// ---- starting and stopping ----------------------------------------------------------------------

bool DeepgramAgent::start()
{
    if (active())
        return true;
    m_error.clear();
    m_notice.clear();
    if (!m_enabled) {
        m_error = tr("Voice conversation is off. Turn it on in Settings > Voice.");
        emit errorChanged();
        return false;
    }
    if (const QString p = problem(); !p.isEmpty()) {
        m_error = p;
        emit errorChanged();
        return false;
    }
    emit errorChanged();
    m_history.clear();
    m_userText.clear();
    m_agentText.clear();
    emit transcriptChanged();
    m_reconnects = 0;
    m_stopping = false;
    m_sent = m_cfg;
    m_restartNeeded = false;
    m_everApplied = false;
    resetStats();
    m_aec.reset();
    m_preroll.clear();
    m_wasEchoGated = false;
    setState(State::Connecting);
    openSocket(false);
    return true;
}

void DeepgramAgent::toggle()
{
    if (active())
        stop();
    else
        start();
}

void DeepgramAgent::stop()
{
    if (!active())
        return;
    m_stopping = true;
    m_ws.close(1000, QStringLiteral("bye"));
    finishSession(true);
}

void DeepgramAgent::openSocket(bool resume)
{
    m_resuming = resume;
    m_settingsApplied = false;
    m_agentAudioDone = true;
    m_dropAgentAudio = false;
    m_lastWsError.clear();
    m_oddByte.clear();
    setState(State::Connecting);
    m_ws.open(deepgram::agentUrl(m_cfg), { { QByteArrayLiteral("Authorization"), deepgram::authorizationHeader(m_cfg.apiKey) } });
}

void DeepgramAgent::onOpened()
{
    // Nothing is sent before the server's Welcome.
}

void DeepgramAgent::finishSession(bool graceful)
{
    const bool was = active();
    m_keepAlive.stop();
    m_idle.stop();
    m_reconnect.stop();
    if (m_capture)
        m_capture->stop();
    m_micBuffer.clear();
    m_preroll.clear();
    m_player.flush();
    m_aec.reset();
    if (was && (m_replies > 0 || m_stalls > 0))
        qInfo().noquote() << "noch voice: conversation ended;" << m_replies << "replies, average"
                          << avgLatencyMs() << "ms to answer," << avgResponseDelayMs()
                          << "ms until the first sound, jitter buffer" << m_player.prebufferMs() << "ms,"
                          << m_stalls << "stalls, echo reduced by" << int(m_aec.erleDb()) << "dB";
    m_settingsApplied = false;
    m_queuedText.clear();
    dropCalls();
    if (m_level != 0.0) {
        m_level = 0.0;
        emit levelChanged();
    }
    // A polite goodbye is allowed to reach the server (WsClient finishes the close
    // handshake by itself); a failure drops the socket at once.
    if (!graceful)
        m_ws.abort();
    setState(State::Off);
    if (was)
        emit ended();
}

// ---- the microphone ----------------------------------------------------------------------------

void DeepgramAgent::startMicrophone()
{
    if (!m_capture)
        m_capture = makeMicrophoneCapture();
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &AudioCapture::audio, this, &DeepgramAgent::onMicAudio);
    connect(m_capture.get(), &AudioCapture::failed, this, [this](const QString &why) {
        if (active())
            fail(why.isEmpty() ? tr("The microphone stopped working.") : why);
    });
    QString why;
    m_micBuffer.clear();
    if (!m_capture->start(&why)) {
        fail(why.isEmpty() ? tr("The microphone could not be opened.") : why);
    }
}

// The microphone is gated, not closed, while the agent speaks through speakers (or
// when muted): silence is sent in its place so the connection stays a normal stream
// and the agent cannot hear itself.
bool DeepgramAgent::echoCancelling() const
{
    return m_echoCancel && m_audioMode == QLatin1String("speakers");
}

// Speaker mode: the microphone also hears the agent through the speakers, so it stays gated while
// the agent talks. A voice clearly louder than that echo, held for a few frames, is the user talking
// over the agent. Headphones never need this: their microphone is never gated.
bool DeepgramAgent::userTalkingOver(bool loud)
{
    if (m_muted || m_audioMode != QLatin1String("speakers") || m_state != State::Speaking) {
        m_bargeFrames = 0;
        return false;
    }
    m_bargeFrames = loud ? m_bargeFrames + 1 : 0;
    return m_bargeFrames >= kBargeFrames;
}

bool DeepgramAgent::gateClosed() const
{
    if (m_muted)
        return true;
    if (m_audioMode != QLatin1String("speakers"))
        return false;
    if (m_dropAgentAudio)
        return false;        // the user just cut the agent off
    return m_state == State::Speaking || !m_player.idle()
           || QDateTime::currentMSecsSinceEpoch() < m_gateOpensAtMs;
}

void DeepgramAgent::onMicAudio(const QByteArray &pcm)
{
    if (!m_settingsApplied || !m_ws.isOpen())
        return;
    m_micBuffer += pcm;
    while (m_micBuffer.size() >= kFrameBytes) {
        QByteArray frame = m_micBuffer.left(kFrameBytes);
        m_micBuffer.remove(0, kFrameBytes);
        processMicFrame(frame);
    }
}

void DeepgramAgent::sendMicFrame(const QByteArray &frame)
{
    if (m_ws.bufferedBytes() > kMaxBacklogBytes)
        return;                 // a stalled network drops audio instead of queueing it
    m_ws.sendBinary(frame);
}

// What the microphone heard just before the gate opened: the loud run that ends now, plus a frame of
// lead-in, so the first syllables of an interruption are not lost to the gate.
void DeepgramAgent::sendPreroll()
{
    qsizetype start = m_preroll.size() - 1;
    while (start > 0 && m_preroll[start - 1].loud)
        --start;
    if (start > 0)
        --start;
    for (qsizetype i = start; i < m_preroll.size(); ++i)
        sendMicFrame(m_preroll[i].pcm);
    m_preroll.clear();
}

// One 40 ms frame of the microphone: echo taken out, held back while the agent talks (speaker mode),
// or sent. The user talking over the agent opens the gate and cuts the agent off.
void DeepgramAgent::processMicFrame(QByteArray frame)
{
    const bool aec = echoCancelling();
    if (aec)
        frame = m_aec.process(frame);
    const double heard = voice::level(frame);
    // How loud the agent's own voice can be at the microphone: a share of what the speakers play,
    // a smaller one once the canceller is taking most of it out.
    const double echo = m_player.level() * (aec && m_aec.converged() ? kBargeEchoRatioAec : kBargeEchoRatio)
                        + kBargeEchoMargin;
    const bool loud = heard > std::max(kBargeMinLevel, echo);

    // The last few frames, but never audio from while the microphone was muted.
    if (m_muted) {
        m_preroll.clear();
    } else {
        m_preroll.append({ frame, loud });
        while (m_preroll.size() > kPrerollFrames)
            m_preroll.removeFirst();
    }

    bool gated = gateClosed();
    bool barged = false;
    if (gated && userTalkingOver(loud)) {
        interrupt();
        gated = gateClosed();
        barged = !gated;
    }
    const bool heldForAgent = gated && !m_muted;

    if (gated) {
        if (m_level != 0.0) {
            m_level = 0.0;
            emit levelChanged();
        }
        sendMicFrame(QByteArray(kFrameBytes, '\0'));
    } else {
        // Rise fast, fall slowly: a meter that follows the voice without flickering.
        m_level = std::max(heard, m_level * 0.82);
        emit levelChanged();
        // The gate has just opened because the user cut in (or the agent's echo is reliably
        // gone): the first words are in the pre-roll, send them first.
        if (m_wasEchoGated && (barged || (aec && m_aec.converged())))
            sendPreroll();
        else
            sendMicFrame(frame);
    }
    m_wasEchoGated = heldForAgent;

    ++m_frames;
    if (aec && m_frames % 25 == 0 && std::abs(m_aec.erleDb() - m_reportedErle) >= 1.0) {
        m_reportedErle = m_aec.erleDb();
        emit statsChanged();
    }
    if (lcDeepgram().isDebugEnabled() && m_frames % 50 == 0)
        qCDebug(lcDeepgram) << "mic frames:" << m_frames << "gated:" << gated << "level" << m_level << "echo cut"
                            << m_aec.erleDb() << "dB, buffered" << m_ws.bufferedBytes();
}

void DeepgramAgent::resetStats()
{
    m_turnEndMs = 0;
    m_responseMs = 0;
    m_responseSum = 0;
    m_responseCount = 0;
    m_latencyMs = 0;
    m_latencySum = 0;
    m_latencyCount = 0;
    m_replies = 0;
    m_stalls = 0;
    m_reportedErle = 0.0;
    m_frames = 0;
    emit latencyChanged();
    emit statsChanged();
}

// The first sound of an answer is coming out of the speakers: how long after the user's last word?
void DeepgramAgent::noteFirstSound()
{
    if (m_turnEndMs <= 0)
        return;
    m_responseMs = int(QDateTime::currentMSecsSinceEpoch() - m_turnEndMs);
    m_responseSum += m_responseMs;
    ++m_responseCount;
    m_turnEndMs = 0;
    qCDebug(lcDeepgram) << "first sound" << m_responseMs << "ms after the last word";
    emit statsChanged();
}

// ---- what the server says ------------------------------------------------------------------------

void DeepgramAgent::onText(const QString &json)
{
    const deepgram::Event e = deepgram::parseEvent(json);
    qCDebug(lcDeepgram).noquote() << "<-" << e.rawType << e.role << e.text.left(60) << e.code;
    handleEvent(e);
}

void DeepgramAgent::onBinary(const QByteArray &data)
{
    if (!m_settingsApplied)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_dropAgentAudio || now < m_dropUntilMs)
        return;
    QByteArray pcm = m_oddByte + data;
    m_oddByte.clear();
    if (pcm.size() & 1) {
        m_oddByte = pcm.right(1);
        pcm.chop(1);
    }
    if (pcm.isEmpty())
        return;
    m_agentAudioDone = false;
    if (m_state != State::Speaking)
        setState(State::Speaking);
    touchActivity();
    m_player.setStreaming(true);     // more of this answer is coming: a stall rebuffers, it does not end it
    m_lastAudioMs = now;
    m_player.write(pcm);
}

void DeepgramAgent::onAgentDrained()
{
    m_lastAudioMs = QDateTime::currentMSecsSinceEpoch();
    m_gateOpensAtMs = m_lastAudioMs + kGateTailMs;
    if (m_agentAudioDone && m_state == State::Speaking) {
        setState(State::Listening);
        if (m_followUp > 0)
            touchActivity();          // the follow-up window starts when it stops talking
        else
            armFollowUp();
    }
}

void DeepgramAgent::remember(const QString &role, const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    m_history.append({ role, text });
    while (m_history.size() > 40)
        m_history.removeFirst();
}

void DeepgramAgent::handleEvent(const deepgram::Event &e)
{
    using E = deepgram::Event;
    switch (e.type) {
    case E::Welcome: {
        deepgram::AgentConfig c = m_cfg;
        if (m_resuming)
            c.history = m_history;
        m_sent.history = c.history;
        QString why;
        const QJsonObject settings = deepgram::buildSettings(c, &why);
        if (!why.isEmpty()) {
            fail(why);
            return;
        }
        m_ws.sendText(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
        break;
    }
    case E::SettingsApplied:
        m_settingsApplied = true;
        m_gateOpensAtMs = 0;
        m_keepAlive.start();
        touchActivity();
        startMicrophone();
        if (!active())
            return;     // the microphone would not open
        setState(State::Listening);
        if (m_resuming) {
            m_resuming = false;
            setNotice(QString());
        }
        for (const QString &t : std::as_const(m_queuedText))
            m_ws.sendText(deepgram::injectUserMessage(t));
        m_queuedText.clear();
        if (!m_everApplied) {
            m_everApplied = true;
            emit started();
        }
        break;
    case E::UserStartedSpeaking:
        // Barge-in: stop talking at once, and ignore the tail of the old answer still on its way.
        m_player.flush();
        m_turnEndMs = 0;
        m_dropUntilMs = QDateTime::currentMSecsSinceEpoch() + kBargeInDropMs;
        m_agentAudioDone = true;
        m_oddByte.clear();
        touchActivity();
        if (m_state == State::Speaking || m_state == State::Thinking)
            setState(State::Listening);
        break;
    case E::ConversationText:
        touchActivity();
        if (e.role == QLatin1String("user")) {
            m_userText = e.text;
            m_turnEndMs = QDateTime::currentMSecsSinceEpoch();     // the clock for "until the first sound"
            m_dropAgentAudio = false;
            m_dropUntilMs = 0;      // the final words are in: whatever audio comes next answers them
            remember(QStringLiteral("user"), e.text);
            emit transcriptChanged();
            emit userSaid(e.text);
            if (m_state == State::Listening)
                setState(State::Thinking);
        } else if (e.role == QLatin1String("assistant")) {
            m_agentText = e.text;
            ++m_replies;
            emit statsChanged();
            m_lastReplyAsked = e.text.trimmed().endsWith(QLatin1Char('?'));
            remember(QStringLiteral("assistant"), e.text);
            emit transcriptChanged();
            emit agentSaid(e.text);
        }
        break;
    case E::AgentThinking:
    case E::EndOfTurn:
        if (m_state == State::Listening)
            setState(State::Thinking);
        break;
    case E::AgentStartedSpeaking:
        m_dropAgentAudio = false;
        break;
    case E::AgentAudioDone:
        m_agentAudioDone = true;
        m_dropAgentAudio = false;
        m_player.setStreaming(false);    // the answer is complete: what is left plays out
        if (m_player.idle() && m_state == State::Speaking) {
            setState(State::Listening);
            if (m_followUp > 0)
                touchActivity();
            else
                armFollowUp();
        } else if (m_state == State::Thinking) {
            setState(State::Listening);
        }
        break;
    case E::FunctionCallRequest:
        touchActivity();
        for (const deepgram::FunctionCall &call : e.calls) {
            if (!call.clientSide)
                continue;           // run on Deepgram's side
            const bool isAsk = call.name == QLatin1String(deepgram::kAskBuddyFunction);
            const bool isAnswer = call.name == QLatin1String(deepgram::kAnswerBuddyFunction);
            const QString request = isAsk ? call.request() : QString();
            const QString answer = isAnswer ? call.answer() : QString();
            if ((!isAsk && !isAnswer) || (isAsk && request.isEmpty()) || (isAnswer && answer.isEmpty())) {
                m_ws.sendText(deepgram::functionResponseMessage(
                    call.id, call.name,
                    isAsk ? QStringLiteral("The request was empty. Ask the user what they want done.")
                    : isAnswer ? QStringLiteral("There was no answer. Ask the user again and pass on exactly what they say.")
                               : QStringLiteral("That function does not exist.")));
                continue;
            }
            // The same id again replaces the call: retire the old one's timer
            // instead of leaving it armed to answer the new call early.
            if (const auto old = m_calls.find(call.id); old != m_calls.end() && old->timeout) {
                old->timeout->stop();
                old->timeout->deleteLater();
            }
            Call c;
            c.name = call.name;
            c.timeout = new QTimer(this);
            c.timeout->setSingleShot(true);
            const QString id = call.id;
            connect(c.timeout, &QTimer::timeout, this, [this, id] {
                emit buddyCancelled(id);
                respondToCall(id, tr("Buddy did not finish in time."));
            });
            c.timeout->start(kBuddyTimeoutMs);
            m_calls.insert(id, c);
            emit buddyBusyChanged();
            m_ackCount = 0;
            m_ackTimer.start(kAckFirstMs);
            if (isAnswer)
                emit buddyAnswered(id, answer);
            else
                emit buddyRequested(id, request);
        }
        break;
    case E::FunctionCallCancelled:
        for (const QString &id : e.cancelledIds) {
            const auto it = m_calls.find(id);
            if (it == m_calls.end())
                continue;
            if (it->timeout) {
                it->timeout->stop();
                it->timeout->deleteLater();
            }
            m_calls.erase(it);
            emit buddyBusyChanged();
            emit buddyCancelled(id);
        }
        break;
    case E::LatencyReport:
        if (e.totalLatency >= 0) {
            m_latencyMs = int(e.totalLatency * 1000.0 + 0.5);
            m_latencySum += m_latencyMs;
            ++m_latencyCount;
            emit latencyChanged();
            emit statsChanged();
        }
        break;
    case E::Warning:
        if (!deepgram::isQuietWarning(e.code)) {
            const QString f = deepgram::friendlyProblem(e.code, e.description);
            setNotice(f.isEmpty() ? e.description.left(240) : f);
        }
        break;
    case E::Error: {
        const QString f = deepgram::friendlyProblem(e.code, e.description);
        fail(f.isEmpty() ? (e.description.isEmpty() ? tr("Deepgram reported an error (%1).").arg(e.code) : e.description.left(300)) : f);
        break;
    }
    default:
        break;
    }
}

void DeepgramAgent::onClosed(quint16 code, const QString &reason, bool clean)
{
    Q_UNUSED(clean);
    if (m_stopping || !active())
        return;
    // A server that closes a good session (a quiet line, the time limit, a restart) or a network
    // that drops: pick the conversation up again where it was, a few times.
    if (m_everApplied && m_reconnects < kMaxReconnects && code != 1008) {
        ++m_reconnects;
        if (m_capture)
            m_capture->stop();
        m_micBuffer.clear();
        m_player.flush();
        m_settingsApplied = false;
        setState(State::Connecting);
        setNotice(tr("Connection lost. Reconnecting…"));
        m_reconnect.start(700 * m_reconnects);
        return;
    }
    QString why = m_lastWsError.isEmpty() ? reason : m_lastWsError;
    if (why.contains(QLatin1String("HTTP 401")) || why.contains(QLatin1String("HTTP 403")))
        why = tr("Deepgram rejected the API key. Check it in Settings > Voice.");
    else if (why.isEmpty())
        why = tr("The connection to Deepgram was lost.");
    fail(why);
}

// ---- Buddy's side of ask_buddy --------------------------------------------------------------------

void DeepgramAgent::respondToCall(const QString &id, const QString &outcome)
{
    const auto it = m_calls.find(id);
    if (it == m_calls.end())
        return;
    const QString name = it->name;
    if (it->timeout) {
        it->timeout->stop();
        it->timeout->deleteLater();
    }
    m_calls.erase(it);
    if (m_calls.isEmpty())
        m_ackTimer.stop();
    emit buddyBusyChanged();
    QString text = outcome.simplified();
    if (text.isEmpty())
        text = tr("Done.");
    if (text.size() > kMaxAnswerChars)
        text = text.left(kMaxAnswerChars) + QStringLiteral("…");
    if (m_ws.isOpen())
        m_ws.sendText(deepgram::functionResponseMessage(id, name, text));
}

void DeepgramAgent::answerBuddy(const QString &callId, const QString &outcome)
{
    respondToCall(callId, outcome);
}

void DeepgramAgent::dropCalls()
{
    m_ackTimer.stop();
    if (m_calls.isEmpty())
        return;
    const QStringList ids = m_calls.keys();
    for (auto it = m_calls.begin(); it != m_calls.end(); ++it) {
        if (it->timeout) {
            it->timeout->stop();
            it->timeout->deleteLater();
        }
    }
    m_calls.clear();
    emit buddyBusyChanged();
    for (const QString &id : ids)
        emit buddyCancelled(id);
}

// ---- things the app can say or do during a conversation ---------------------------------------------------

void DeepgramAgent::interrupt()
{
    if (!active() || m_state != State::Speaking)
        return;
    m_player.flush();
    m_oddByte.clear();
    m_dropAgentAudio = true;        // the rest of this answer is not wanted; the mic opens
    m_agentAudioDone = true;
    setState(State::Listening);
}

void DeepgramAgent::holdMic(int ms)
{
    if (ms <= 0)
        return;
    m_gateOpensAtMs = std::max(m_gateOpensAtMs, QDateTime::currentMSecsSinceEpoch() + ms);
}

void DeepgramAgent::say(const QString &text)
{
    const QString t = text.simplified().left(300);
    if (t.isEmpty() || !m_settingsApplied || !m_ws.isOpen())
        return;
    m_ws.sendText(deepgram::injectAgentMessage(t, QStringLiteral("queue")));
}

void DeepgramAgent::queueText(const QString &text)
{
    const QString t = text.simplified();
    if (t.isEmpty())
        return;
    if (m_settingsApplied && m_ws.isOpen())
        m_ws.sendText(deepgram::injectUserMessage(t));
    else if (active())
        m_queuedText << t;
}

void DeepgramAgent::sendText(const QString &text)
{
    const QString t = text.simplified();
    if (t.isEmpty() || !m_settingsApplied || !m_ws.isOpen())
        return;
    m_ws.sendText(deepgram::injectUserMessage(t));
}

// ---- connection test -------------------------------------------------------------------------------

void DeepgramAgent::test()
{
    if (m_testState == QLatin1String("testing"))
        return;
    if (active()) {
        m_testState = QStringLiteral("failed");
        m_testMessage = tr("End the conversation first, then test.");
        emit testChanged();
        return;
    }
    if (const QString p = problem(); !p.isEmpty()) {
        m_testState = QStringLiteral("failed");
        m_testMessage = p;
        emit testChanged();
        return;
    }
    m_testState = QStringLiteral("testing");
    m_testMessage = tr("Connecting to Deepgram…");
    m_testApplied = false;
    m_testWarning.clear();
    emit testChanged();
    m_testTimeout.start(kTestTimeoutMs);
    m_testWs.open(deepgram::agentUrl(m_cfg), { { QByteArrayLiteral("Authorization"), deepgram::authorizationHeader(m_cfg.apiKey) } });
    // A refusal (wrong key) arrives as an error before any message.
    disconnect(m_testErrConn);
    m_testErrConn = connect(&m_testWs, &WsClient::errorOccurred, this, [this](const QString &why) {
        if (m_testState != QLatin1String("testing"))
            return;
        finishTest(false, (why.contains(QLatin1String("HTTP 401")) || why.contains(QLatin1String("HTTP 403")))
                              ? tr("Deepgram rejected the API key.") : why);
    });
}

void DeepgramAgent::finishTest(bool ok, const QString &message)
{
    if (m_testState != QLatin1String("testing"))
        return;
    m_testTimeout.stop();
    m_testState = ok ? QStringLiteral("ok") : QStringLiteral("failed");
    m_testMessage = message;
    m_testWs.abort();
    emit testChanged();
}

// ---- the lists in Settings --------------------------------------------------------------------------

void DeepgramAgent::refreshModels()
{
    if (!deepgram::keyLooksValid(m_cfg.apiKey)) {
        m_modelsProblem = tr("Add your Deepgram API key to load the lists.");
        emit modelsChanged();
        return;
    }
    for (QPointer<QNetworkReply> *r : { &m_thinkReply, &m_voicesReply }) {
        if (*r) {
            QNetworkReply *old = *r;
            *r = nullptr;
            old->disconnect(this);
            old->abort();
            old->deleteLater();
        }
    }
    m_modelsProblem.clear();
    m_modelsPending = 2;
    emit modelsChanged();

    auto fetch = [this](const QUrl &url, int timeoutMs, QPointer<QNetworkReply> *slot,
                        std::function<void(const QByteArray &)> parse) {
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", deepgram::authorizationHeader(m_cfg.apiKey));
        req.setTransferTimeout(timeoutMs);
        QNetworkReply *reply = m_nam.get(req);
        *slot = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, slot, parse = std::move(parse)] {
            if (slot->data() != reply)
                return;           // superseded
            *slot = nullptr;
            reply->deleteLater();
            --m_modelsPending;
            const QByteArray body = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
                QString why = deepgram::restErrorMessage(body);
                if (status == 401 || status == 403)
                    why = tr("Deepgram rejected the API key.");
                else if (why.isEmpty())
                    why = status > 0 ? tr("Deepgram answered HTTP %1.").arg(status) : tr("Could not reach Deepgram.");
                m_modelsProblem = why;
            } else {
                parse(body);
            }
            emit modelsChanged();
        });
    };
    fetch(QUrl(m_thinkModelsUrl), 20000, &m_thinkReply, [this](const QByteArray &body) {
        const QVariantList list = deepgram::thinkModelsFromJson(body);
        if (!list.isEmpty())
            m_thinkModels = list;
        else
            m_modelsProblem = tr("Deepgram sent no language models.");
    });
    // The full voice catalogue is large and slow (it lists every model Deepgram has), so give it time.
    fetch(QUrl(m_apiBase + QStringLiteral("/v1/models")), 60000, &m_voicesReply, [this](const QByteArray &body) {
        const QVariantList list = deepgram::voicesFromModels(body);
        if (!list.isEmpty())
            m_voices = list;
    });
}
