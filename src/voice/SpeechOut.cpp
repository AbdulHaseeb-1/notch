#include "SpeechOut.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <utility>

#include "DeepgramProtocol.h"
#include "VoiceAudio.h"
#include "WakeWord.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

bool isLocalHost(const QString &host)
{
    const QString h = host.toLower();
    return h == QLatin1String("localhost") || h == QLatin1String("127.0.0.1") || h == QLatin1String("::1")
        || h == QLatin1String("[::1]");
}

QString apiMessage(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QJsonValue err = o.value(QStringLiteral("error"));
    return (err.isObject() ? err.toObject().value(QStringLiteral("message")).toString() : err.toString()).left(300);
}

// Gemini TTS, through the Google AI Studio key. The Interactions API returns
// the final audio as a content part; the older output_audio shape is accepted
// too so a service update cannot silence Buddy.
const QString kGoogleTtsModel = QStringLiteral("gemini-3.8-flash-lite-tts");

struct GoogleAudio
{
    QString data;
    QString mimeType;
};

GoogleAudio googleAudioPart(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray steps = root.value(QStringLiteral("steps")).toArray();
    for (int i = steps.size() - 1; i >= 0; --i) {
        const QJsonObject step = steps.at(i).toObject();
        if (step.value(QStringLiteral("type")).toString() != QLatin1String("model_output"))
            continue;
        const QJsonArray content = step.value(QStringLiteral("content")).toArray();
        for (int j = content.size() - 1; j >= 0; --j) {
            const QJsonObject part = content.at(j).toObject();
            if (part.value(QStringLiteral("type")).toString() == QLatin1String("audio")) {
                return { part.value(QStringLiteral("data")).toString(),
                         part.value(QStringLiteral("mime_type")).toString() };
            }
        }
    }
    const QJsonObject output = root.value(QStringLiteral("output_audio")).toObject();
    return { output.value(QStringLiteral("data")).toString(),
             output.value(QStringLiteral("mime_type")).toString() };
}

QString normalisedGoogleVoice(const QString &voice)
{
    const QString trimmed = voice.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("Kore");
    // Cloud TTS names encoded a locale, for example en-US-Neural2-F. Gemini
    // voices are names such as Kore or Puck (or custom voice_... IDs), so an
    // old saved name is replaced rather than sent to the new API.
    static const QRegularExpression oldCloudVoice(QStringLiteral("^[a-z]{2,3}(?:-[A-Za-z]{2,4})?-"));
    if (oldCloudVoice.match(trimmed).hasMatch())
        return QStringLiteral("Kore");
    return trimmed;
}

} // namespace

SpeechOut::SpeechOut(QObject *parent)
    : QObject(parent)
{
    defaultCommand(&m_program, &m_args);
#ifdef Q_OS_WIN
    // PowerShell is a console program: do not flash a console window.
    m_proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    connect(&m_proc, &QProcess::started, this, &SpeechOut::setSpeaking);
    connect(&m_proc, &QProcess::finished, this, &SpeechOut::setSpeaking);
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            setSpeaking();
    });
    connect(&m_player, &SpeechPlayer::finished, this, &SpeechOut::setSpeaking);
    connect(&m_player, &SpeechPlayer::finished, this, &SpeechOut::onPlayerChunkFinished);
    m_stream.setInputRate(24000);
    connect(&m_stream, &StreamingPlayer::drained, this, [this] {
        // The last sentence has been fetched and has now been heard to its end.
        if (m_streaming && m_streamChunks.isEmpty() && !m_streamReply)
            m_streaming = false;
        setSpeaking();
    });
    connect(&m_stream, &StreamingPlayer::failed, this, [this](const QString &why) { onStreamFailure(why); });
}

SpeechOut::~SpeechOut()
{
    stop();
}

void SpeechOut::defaultCommand(QString *program, QStringList *args)
{
    program->clear();
    args->clear();
#ifdef Q_OS_WIN
    *program = QStringLiteral("powershell.exe");
    *args = { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
              QStringLiteral("[Console]::InputEncoding=[System.Text.Encoding]::UTF8;"
                             "Add-Type -AssemblyName System.Speech;"
                             "$t=[Console]::In.ReadToEnd();"
                             "$s=New-Object System.Speech.Synthesis.SpeechSynthesizer;"
                             "$s.Speak($t)") };
#else
    for (const char *name : { "espeak-ng", "espeak" }) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty()) {
            *program = found;
            *args = { QStringLiteral("--stdin") };
            return;
        }
    }
#endif
}

void SpeechOut::setCommand(const QString &program, const QStringList &args)
{
    stop();
    m_program = program;
    m_args = args;
    emit configChanged();
}

// ---- configuration ----------------------------------------------------------------------------

QString SpeechOut::kokoroAddressProblem(const QString &base)
{
    const QString trimmed = base.trimmed();
    if (trimmed.isEmpty())
        return tr("Add the address of your Kokoro server.");
    const QUrl u = QUrl::fromUserInput(trimmed);
    if (!u.isValid() || u.host().isEmpty())
        return tr("That address does not look right.");
    if (u.scheme() == QLatin1String("https"))
        return QString();
    if (u.scheme() == QLatin1String("http") && isLocalHost(u.host()))
        return QString();
    return tr("Speech is only sent over https, or to this computer.");
}

QString SpeechOut::kokoroUrl(const QString &base)
{
    if (!kokoroAddressProblem(base).isEmpty())
        return QString();
    QString b = QUrl::fromUserInput(base.trimmed()).toString();
    while (b.endsWith(QLatin1Char('/')))
        b.chop(1);
    if (b.endsWith(QLatin1String("/audio/speech")))
        return b;
    if (b.endsWith(QLatin1String("/v1")))
        return b + QStringLiteral("/audio/speech");
    return b + QStringLiteral("/v1/audio/speech");
}

QString SpeechOut::kokoroVoicesUrl(const QString &base)
{
    const QString speech = kokoroUrl(base);
    static const QString kSpeechPath = QStringLiteral("/audio/speech");
    if (!speech.endsWith(kSpeechPath))
        return QString();
    return speech.left(speech.size() - kSpeechPath.size()) + QStringLiteral("/audio/voices");
}

QString SpeechOut::problem() const
{
    return problemFor(m_engine);
}

QString SpeechOut::problemFor(const QString &engine) const
{
    if (engine == QLatin1String("google"))
        return m_googleKey.trimmed().isEmpty() ? tr("Add a Google AI Studio API key to use this voice.") : QString();
    if (engine == QLatin1String("deepgram")) {
        if (m_deepgramKey.isEmpty())
            return tr("Add your Deepgram API key to use this voice.");
        if (!deepgram::keyLooksValid(m_deepgramKey))
            return tr("That does not look like a Deepgram API key.");
        return m_deepgramVoice.trimmed().isEmpty() ? tr("Choose a Deepgram voice.") : QString();
    }
    if (engine == QLatin1String("kokoro")) {
        if (const QString p = kokoroAddressProblem(m_kokoroBase); !p.isEmpty())
            return p;
        return m_kokoroVoice.trimmed().isEmpty() ? tr("Add a Kokoro voice name, for example af_heart.") : QString();
    }
    return m_program.isEmpty() ? tr("No voice is installed on this computer.") : QString();
}

QVariantMap SpeechOut::engineStatuses() const
{
    QVariantMap out;
    auto entry = [](bool ready, const QString &detail) {
        return QVariantMap{ { QStringLiteral("ready"), ready }, { QStringLiteral("detail"), detail } };
    };
    out[QStringLiteral("system")] = entry(!m_program.isEmpty(),
                                          m_program.isEmpty() ? tr("No system voice program was found.")
                                                              : tr("Reads with %1.").arg(m_program));
    out[QStringLiteral("google")] = entry(!m_googleKey.trimmed().isEmpty(),
                                          m_googleKey.trimmed().isEmpty()
                                              ? tr("Add a Google AI Studio API key (aistudio.google.com/apikey).")
                                              : tr("Voice %1 with Gemini TTS.").arg(m_googleVoice));
    const bool dgKey = deepgram::keyLooksValid(m_deepgramKey);
    out[QStringLiteral("deepgram")] = entry(dgKey && !m_deepgramVoice.isEmpty(),
                                            m_deepgramKey.isEmpty() ? tr("Add your Deepgram API key (console.deepgram.com).")
                                            : !dgKey ? tr("That does not look like a Deepgram API key.")
                                                     : tr("Voice %1 with Deepgram Aura.").arg(deepgram::voiceLabel(m_deepgramVoice)));
    const QString kp = kokoroAddressProblem(m_kokoroBase);
    out[QStringLiteral("kokoro")] = entry(kp.isEmpty() && !m_kokoroVoice.trimmed().isEmpty(),
                                          kp.isEmpty() ? tr("Voice %1 on %2.").arg(m_kokoroVoice, m_kokoroBase)
                                                       : kp);
    return out;
}

void SpeechOut::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        stop();
    emit configChanged();
}

void SpeechOut::setEngine(const QString &engine)
{
    const QString e = (engine == QLatin1String("google") || engine == QLatin1String("kokoro")
                       || engine == QLatin1String("deepgram")) ? engine : QStringLiteral("system");
    if (e == m_engine)
        return;
    stop();
    m_engine = e;
    emit configChanged();
}

#define NOCH_SPEECH_SETTER(Name, member, normalize)       \
    void SpeechOut::Name(const QString &value)            \
    {                                                     \
        const QString v = normalize;                      \
        if (v == member)                                  \
            return;                                       \
        member = v;                                       \
        emit configChanged();                             \
    }
NOCH_SPEECH_SETTER(setGoogleKey, m_googleKey, value.trimmed())
NOCH_SPEECH_SETTER(setDeepgramKey, m_deepgramKey, value.trimmed())
NOCH_SPEECH_SETTER(setDeepgramVoice, m_deepgramVoice, value.trimmed())
NOCH_SPEECH_SETTER(setKokoroVoice, m_kokoroVoice, value.trimmed())
#undef NOCH_SPEECH_SETTER

void SpeechOut::setKokoroBase(const QString &base)
{
    const QString v = base.trimmed();
    if (v == m_kokoroBase)
        return;
    m_kokoroBase = v;
    // The old list belongs to the old server: drop it, and any fetch for it.
    if (m_voicesReply) {
        QNetworkReply *old = m_voicesReply;
        m_voicesReply = nullptr;
        old->abort();
        old->deleteLater();
    }
    m_kokoroVoices.clear();
    m_kokoroVoicesProblem.clear();
    emit kokoroVoicesChanged();
    emit configChanged();
}

void SpeechOut::refreshKokoroVoices()
{
    if (m_voicesReply) {
        QNetworkReply *old = m_voicesReply;
        m_voicesReply = nullptr;
        old->abort();
        old->deleteLater();
    }
    m_kokoroVoicesProblem.clear();
    const QString url = kokoroVoicesUrl(m_kokoroBase);
    if (url.isEmpty()) {
        m_kokoroVoicesProblem = kokoroAddressProblem(m_kokoroBase);
        emit kokoroVoicesChanged();
        return;
    }
    QNetworkRequest request{ QUrl(url) };
    request.setTransferTimeout(10000);
    m_voicesReply = m_nam.get(request);
    QNetworkReply *reply = m_voicesReply;
    emit kokoroVoicesChanged();   // loading
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_voicesReply)
            return;
        m_voicesReply = nullptr;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString why = apiMessage(body);
            if (why.isEmpty())
                why = status > 0 ? tr("The Kokoro server answered HTTP %1.").arg(status)
                                 : tr("Could not reach the Kokoro server. Is it running?");
            m_kokoroVoicesProblem = why;
            emit kokoroVoicesChanged();
            return;
        }
        const QJsonArray voices =
            QJsonDocument::fromJson(body).object().value(QStringLiteral("voices")).toArray();
        QStringList found;
        for (const QJsonValue &entry : voices) {
            // Kokoro-FastAPI returns objects; our server returns plain names.
            QString name = entry.isString() ? entry.toString()
                                            : entry.toObject().value(QStringLiteral("id")).toString();
            if (!entry.isString() && name.isEmpty())
                name = entry.toObject().value(QStringLiteral("name")).toString();
            name = name.trimmed();
            if (!name.isEmpty() && !found.contains(name))
                found.append(name);
        }
        if (found.isEmpty()) {
            m_kokoroVoicesProblem = tr("The server did not list any voices.");
        } else {
            m_kokoroVoices = found;
            m_kokoroVoicesProblem.clear();
        }
        emit kokoroVoicesChanged();
    });
}

void SpeechOut::setGoogleVoice(const QString &voice)
{
    const QString v = normalisedGoogleVoice(voice);
    if (v == m_googleVoice)
        return;
    m_googleVoice = v;
    emit configChanged();
}

void SpeechOut::setSpeed(double speed)
{
    const double v = std::clamp(speed, 0.7, 1.5);
    if (qFuzzyCompare(v, m_speed))
        return;
    m_speed = v;
    emit configChanged();
}

void SpeechOut::setMaxChars(int chars)
{
    const int v = std::clamp(chars, 120, 2000);
    if (v == m_maxChars)
        return;
    m_maxChars = v;
    emit configChanged();
}

void SpeechOut::setSpeakAll(bool on)
{
    if (on == m_speakAll)
        return;
    m_speakAll = on;
    emit configChanged();
}

void SpeechOut::setError(const QString &why)
{
    if (why == m_error)
        return;
    m_error = why;
    emit errorChanged();
}

// ---- speaking ---------------------------------------------------------------------------------

bool SpeechOut::speaking() const
{
    return m_synthesizing || m_streaming || !m_stream.idle() || m_player.playing()
           || m_proc.state() != QProcess::NotRunning;
}

void SpeechOut::setSpeaking()
{
    const bool now = speaking();
    if (now == m_lastSpeaking)
        return;
    m_lastSpeaking = now;
    emit speakingChanged();
}

void SpeechOut::speakReport(const QString &reply)
{
    const QString text = wake::spokenSummary(reply, m_maxChars);
    if (!text.isEmpty())
        start(text);
}

QString SpeechOut::summary(const QString &reply) const
{
    return wake::spokenSummary(reply, m_maxChars);
}

void SpeechOut::say(const QString &line)
{
    const QString t = line.simplified().left(200);
    if (!t.isEmpty())
        start(t);
}

void SpeechOut::test()
{
    start(tr("Hi, I'm Buddy. This is how I sound."), true);
}

void SpeechOut::previewDeepgram()
{
    start(tr("Hi, I'm Buddy. This is how I sound."), true, QStringLiteral("deepgram"));
}

void SpeechOut::start(const QString &text, bool force, const QString &engineOverride)
{
    if (!force && !m_enabled)
        return;
    stop();
    setError(QString());
    ++m_generation;
    m_activeEngine = engineOverride.isEmpty() ? m_engine : engineOverride;
    if (m_activeEngine == QLatin1String("google") && !m_googleKey.trimmed().isEmpty()) {
        beginPipeline(text);
    } else if (m_activeEngine == QLatin1String("deepgram") && problemFor(m_activeEngine).isEmpty()) {
        beginDeepgramStream(text);
    } else if (m_activeEngine == QLatin1String("kokoro") && problemFor(m_activeEngine).isEmpty()) {
        beginPipeline(text);
    } else {
        if (m_activeEngine != QLatin1String("system"))
            setError(problemFor(m_activeEngine) + QLatin1Char(' ') + tr("Using the system voice."));
        startSystem(text);
    }
}

void SpeechOut::startSystem(const QString &text)
{
    if (m_program.isEmpty())
        return;
    m_proc.setProgram(m_program);
#ifdef Q_OS_WIN
    // The default PowerShell voice ignored the speed setting before: when it
    // differs from 1.0, speak the text as SSML with a prosody rate instead.
    // Only the default command is reshaped; a user-set command stays as-is.
    const bool ssml = !qFuzzyCompare(m_speed, 1.0) && m_args.size() == 4
                      && m_args.at(2) == QStringLiteral("-Command")
                      && m_args.at(3).contains(QStringLiteral("SpeechSynthesizer"));
    if (ssml) {
        QString script = m_args.at(3);
        script.replace(QStringLiteral("$s.Speak($t)"), QStringLiteral("$s.SpeakSsml($t)"));
        QStringList args = m_args;
        args[3] = script;
        m_proc.setArguments(args);
        const int rate = qBound(50, qRound(m_speed * 100.0), 200);
        QString escaped = text;
        escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
            .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
            .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        m_proc.start();
        setSpeaking();
        m_proc.write(QStringLiteral("<speak version=\"1.0\" xmlns=\"http://www.w3.org/2001/10/synthesis\" xml:lang=\"en-US\"><prosody rate=\"%1%\">%2</prosody></speak>")
                         .arg(rate)
                         .arg(escaped)
                         .toUtf8());
        m_proc.closeWriteChannel();
        return;
    }
#endif
    m_proc.setArguments(m_args);
    m_proc.start();
    setSpeaking();   // starting counts as speaking: the listener must not hear it
    // Written once and closed: the program reads until the end of its input.
    m_proc.write(text.toUtf8());
    m_proc.closeWriteChannel();
}

// ---- Deepgram, streamed ----------------------------------------------------------------------
// One request per sentence, one after the other, each streamed into the same player as it
// arrives (raw 24 kHz samples), so the voice starts after the first bytes of the first sentence
// and never leaves a gap between sentences: the next request is made while the last one plays.

void SpeechOut::beginDeepgramStream(const QString &text)
{
    m_streamChunks = splitSpeechChunks(text);
    if (m_streamChunks.isEmpty()) {
        setSpeaking();
        return;
    }
    m_streaming = true;
    m_streamOdd.clear();
    streamNextChunk();
    setSpeaking();
}

void SpeechOut::streamNextChunk()
{
    if (!m_streaming || m_streamReply)
        return;
    if (m_streamChunks.isEmpty()) {
        // Everything is fetched: the player's drained() ends the speech once it has been heard.
        if (m_stream.idle()) {
            m_streaming = false;
            setSpeaking();
        }
        return;
    }
    m_streamCurrent = m_streamChunks.takeFirst();
    const quint64 gen = m_generation;
    QNetworkRequest request{ deepgram::speakUrl(m_deepgramVoice, 24000, m_speed, m_deepgramBase, QStringLiteral("none")) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", deepgram::authorizationHeader(m_deepgramKey));
    request.setTransferTimeout(30000);
    m_streamReply = m_nam.post(request, QJsonDocument(QJsonObject{ { QStringLiteral("text"), m_streamCurrent } }).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_streamReply;
    // Play what has arrived so far, whole samples only.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, gen] {
        if (gen != m_generation || reply != m_streamReply)
            return;
        // An error body is JSON, not audio: wait for finished() to read it.
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 300)
            return;
        QByteArray pcm = m_streamOdd + reply->readAll();
        m_streamOdd.clear();
        if (pcm.size() & 1) {
            m_streamOdd = pcm.right(1);
            pcm.chop(1);
        }
        if (!pcm.isEmpty())
            m_stream.write(pcm);
        setSpeaking();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        if (m_streamReply == reply)
            m_streamReply = nullptr;
        reply->deleteLater();
        if (gen != m_generation || !m_streaming)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QByteArray body = reply->readAll();
            QString why = deepgram::restErrorMessage(body);
            if (status == 401 || status == 403)
                why = tr("Deepgram rejected the API key.");
            else if (status == 402)
                why = tr("Your Deepgram account is out of credit.");
            else if (status == 429)
                why = tr("Deepgram is limiting requests right now.");
            else if (why.isEmpty())
                why = status > 0 ? tr("Deepgram answered HTTP %1.").arg(status) : tr("Could not reach Deepgram.");
            onStreamFailure(why);
            return;
        }
        // Any bytes that came with the last read.
        QByteArray rest = m_streamOdd + reply->readAll();
        m_streamOdd.clear();
        if (rest.size() & 1)
            rest.chop(1);
        if (!rest.isEmpty())
            m_stream.write(rest);
        streamNextChunk();
    });
}

// A voice that failed must not mean a reply that is never heard: what has not been spoken is
// read by the system voice, and the reason is kept where Settings can show it.
void SpeechOut::onStreamFailure(const QString &why)
{
    QStringList unsaid;
    if (!m_streamCurrent.isEmpty() && m_streaming)
        unsaid << m_streamCurrent;
    unsaid << m_streamChunks;
    const QString text = unsaid.join(QLatin1Char(' '));
    const bool wasStreaming = m_streaming;
    stopStream();
    ++m_generation;
    if (!wasStreaming)
        return;
    setError(why + QLatin1Char(' ') + (m_program.isEmpty() ? QString() : tr("Using the system voice.")));
    startSystem(text);
    setSpeaking();
}

void SpeechOut::stopStream()
{
    m_streaming = false;
    m_streamChunks.clear();
    m_streamCurrent.clear();
    m_streamOdd.clear();
    if (m_streamReply) {
        QNetworkReply *r = m_streamReply;
        m_streamReply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_stream.flush();
}

// ---- pipelined cloud speech ---------------------------------------------------------------
// A reply is spoken in sentence chunks: chunk i+1 is synthesized while chunk i
// plays, so the first sound starts after one sentence's synthesis instead of
// the whole reply's. The player handles one chunk at a time; the finished
// signal advances the pipeline. Any chunk that fails falls back to reading
// the rest with the system voice, so a reply is never left half-heard.

QStringList SpeechOut::splitSpeechChunks(const QString &text)
{
    static const int kMaxChunk = 320;
    static const int kMergeBelow = 60;
    static const QRegularExpression sentenceEnd(QStringLiteral("([.!?…]+)(\\s+|$)"));
    const QString s = text.simplified();
    if (s.isEmpty())
        return {};

    QStringList sentences;
    int pos = 0;
    auto it = sentenceEnd.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        sentences << s.mid(pos, m.capturedEnd() - pos).trimmed();
        pos = m.capturedEnd();
    }
    if (pos < s.size())
        sentences << s.mid(pos).trimmed();

    // Short fragments read oddly alone, so they join the next sentence.
    QStringList chunks;
    for (const QString &sent : sentences) {
        if (sent.isEmpty())
            continue;
        if (!chunks.isEmpty() && chunks.last().size() < kMergeBelow)
            chunks.last() += QLatin1Char(' ') + sent;
        else
            chunks << sent;
    }
    // Bound the worst case: a long sentence is cut at word boundaries.
    QStringList out;
    for (const QString &c : chunks) {
        if (c.size() <= kMaxChunk) {
            out << c;
            continue;
        }
        int from = 0;
        while (from < c.size()) {
            int take = qMin(kMaxChunk, c.size() - from);
            if (from + take < c.size()) {
                const int sp = c.lastIndexOf(QLatin1Char(' '), from + take);
                if (sp > from + kMaxChunk / 2)
                    take = sp - from;
            }
            out << c.mid(from, take).trimmed();
            from += take;
            while (from < c.size() && c.at(from).isSpace())
                ++from;
        }
    }
    return out;
}

void SpeechOut::beginPipeline(const QString &text)
{
    m_chunks = splitSpeechChunks(text);
    if (m_chunks.isEmpty()) {
        setSpeaking();
        return;
    }
    m_pipeline = true;
    m_synthesizing = true;
    synthesizeNext();
    setSpeaking();
}

void SpeechOut::synthesizeNext()
{
    if (!m_pipeline || m_chunks.isEmpty())
        return;
    m_currentChunk = m_chunks.takeFirst();
    const quint64 gen = m_generation;
    const bool google = m_activeEngine == QLatin1String("google");
    QNetworkRequest request{ QUrl(google ? m_googleEndpoint : kokoroUrl(m_kokoroBase)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    if (google) {
        // The key goes in a header, not the URL, so it does not end up in any log of addresses.
        request.setRawHeader("X-Goog-Api-Key", m_googleKey.trimmed().toUtf8());
        request.setTransferTimeout(60000);
        // Gemini takes the transcript and its delivery separately. A speed
        // setting changes the style hint; the text itself stays verbatim.
        QJsonObject textPart{ { QStringLiteral("type"), QStringLiteral("text") },
                              { QStringLiteral("text"), m_currentChunk } };
        if (!qFuzzyCompare(m_speed, 1.0)) {
            textPart[QStringLiteral("annotations")] = QJsonArray{
                QJsonObject{ { QStringLiteral("type"), QStringLiteral("speech_metadata") },
                             { QStringLiteral("style"), m_speed < 1.0 ? QStringLiteral("speaking slowly")
                                                                       : QStringLiteral("speaking rapidly") } }
            };
        }
        body = {
            { QStringLiteral("model"), kGoogleTtsModel },
            { QStringLiteral("input"), QJsonArray{
                  QJsonObject{ { QStringLiteral("type"), QStringLiteral("user_input") },
                               { QStringLiteral("content"), QJsonArray{ textPart } } } } },
            { QStringLiteral("response_format"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("audio") },
                                                              { QStringLiteral("mime_type"), QStringLiteral("audio/wav") },
                                                              { QStringLiteral("sample_rate"), 24000 } } },
            { QStringLiteral("generation_config"), QJsonObject{
                  { QStringLiteral("speech_config"), QJsonArray{
                        QJsonObject{ { QStringLiteral("voice"), m_googleVoice } } } } } },
            { QStringLiteral("store"), false },
        };
    } else {
        request.setTransferTimeout(60000);
        body = {
            { QStringLiteral("model"), QStringLiteral("kokoro") },
            { QStringLiteral("input"), m_currentChunk },
            { QStringLiteral("voice"), m_kokoroVoice },
            { QStringLiteral("response_format"), QStringLiteral("wav") },
            { QStringLiteral("speed"), m_speed },
        };
    }
    m_reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, google] {
        if (m_reply == reply)
            m_reply = nullptr;   // no request in flight while this answer is handled
        reply->deleteLater();
        if (gen != m_generation || !m_pipeline)
            return;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString why = apiMessage(data);
            if (google) {
                if (status == 400 || status == 403 || status == 401)
                    why = why.isEmpty() ? tr("Google AI Studio refused the request (HTTP %1). Check the API key.").arg(status)
                                        : tr("Google: %1").arg(why);
                else if (why.isEmpty())
                    why = status > 0 ? tr("Gemini TTS answered HTTP %1.").arg(status)
                                     : tr("Could not reach Google AI Studio.");
            } else if (why.isEmpty()) {
                why = status > 0 ? tr("The Kokoro server answered HTTP %1.").arg(status)
                                 : tr("Could not reach the Kokoro server. Is it running?");
            }
            onFailure(why, remainingSpeech());
            return;
        }
        QByteArray wav = data;
        if (google) {
            const GoogleAudio audio = googleAudioPart(data);
            if (audio.data.isEmpty()) {
                onFailure(tr("Google returned no audio."), remainingSpeech());
                return;
            }
            const QByteArray bytes = QByteArray::fromBase64(audio.data.toLatin1());
            if (bytes.isEmpty()) {
                onFailure(tr("Google returned empty audio."), remainingSpeech());
                return;
            }
            // Ask for WAV; accept headerless 24 kHz L16 if that is what came back.
            wav = (bytes.startsWith("RIFF") || audio.mimeType.contains(QLatin1String("wav"), Qt::CaseInsensitive))
                      ? bytes
                      : voice::encodeWav(bytes, 24000);
        }
        onAudio(wav, remainingSpeech());
    });
    setSpeaking();
}

QString SpeechOut::remainingSpeech() const
{
    QStringList parts;
    if (!m_currentChunk.isEmpty())
        parts << m_currentChunk;
    parts << m_chunks;
    return parts.join(QLatin1Char(' '));
}

void SpeechOut::onChunkAudio(const QByteArray &pcm, int rate, int channels)
{
    if (!m_player.play(pcm, rate, channels)) {
        onFailure(tr("The sound could not be played."), remainingSpeech());
        return;
    }
    setSpeaking();
    synthesizeNext();   // overlap: the next chunk is synthesized while this one plays
}

void SpeechOut::onPlayerChunkFinished()
{
    if (!m_pipeline)
        return;
    if (!m_bufferedPcm.isEmpty()) {
        // Chunk i+1 arrived while chunk i played: start it and synthesize i+2.
        const QByteArray pcm = std::exchange(m_bufferedPcm, QByteArray());
        const int rate = m_bufferedRate, channels = m_bufferedChannels;
        m_bufferedRate = m_bufferedChannels = 0;
        onChunkAudio(pcm, rate, channels);
        return;
    }
    if (m_chunks.isEmpty()) {
        // The pipeline has drained only when no next chunk is still being
        // synthesized. A short chunk can finish playing before its follower
        // arrives; ending here would drop the rest of the reply.
        if (m_reply)
            return;
        m_pipeline = false;
        m_synthesizing = false;
        m_currentChunk.clear();
        setSpeaking();
    }
    // Otherwise the next chunk's synthesis is still in flight; when it
    // arrives, onAudio sees an idle player and starts it at once.
}

void SpeechOut::onAudio(const QByteArray &wav, const QString &text)
{
    int rate = 0, channels = 0;
    QString why;
    const QByteArray pcm = SpeechPlayer::wavToPcm(wav, &rate, &channels, &why);
    if (pcm.isEmpty()) {
        onFailure(why, text);
        return;
    }
    if (!m_pipeline) {
        if (!m_player.play(pcm, rate, channels))
            onFailure(tr("The sound could not be played."), text);
        setSpeaking();
        return;
    }
    // The player is still reading the previous chunk: hold this one until it
    // finishes, instead of cutting the sentence short.
    if (m_player.playing()) {
        m_bufferedPcm = pcm;
        m_bufferedRate = rate;
        m_bufferedChannels = channels;
        return;
    }
    onChunkAudio(pcm, rate, channels);
}

// A voice that failed must not mean a reply that is never heard: say it with the
// system voice and keep the reason where the settings can show it.
void SpeechOut::onFailure(const QString &why, const QString &text)
{
    // A pipeline that failed must stop its cloud work at once: clear it, stop
    // what is playing, and say the rest with the system voice.
    m_pipeline = false;
    m_synthesizing = false;
    m_chunks.clear();
    m_currentChunk.clear();
    m_bufferedPcm.clear();
    m_bufferedRate = m_bufferedChannels = 0;
    m_player.stop();
    setError(why + QLatin1Char(' ') + (m_program.isEmpty() ? QString() : tr("Using the system voice.")));
    startSystem(text);
    setSpeaking();
}

void SpeechOut::stop()
{
    stopStream();
    ++m_generation;       // a reply still on its way is dropped
    m_pipeline = false;
    m_chunks.clear();
    m_currentChunk.clear();
    m_bufferedPcm.clear();
    m_bufferedRate = m_bufferedChannels = 0;
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_synthesizing = false;
    m_player.stop();
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(500);
    }
    setSpeaking();
}
