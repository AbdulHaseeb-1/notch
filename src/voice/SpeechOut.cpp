#include "SpeechOut.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <utility>

#include "DeepgramProtocol.h"
#include "VoiceAudio.h"
#include "WakeWord.h"

namespace {

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
    // The fallback settings object is the live one until a shared one is
    // attached, so edits to it refresh availability just the same.
    connect(&m_fallbackSettings, &DeepgramSettings::configChanged, this, [this] { emit configChanged(); });
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

// ---- configuration ----------------------------------------------------------------------------

QString SpeechOut::problem() const
{
    return problemFor(m_engine);
}

QString SpeechOut::problemFor(const QString &engine) const
{
    if (engine == QLatin1String("deepgram")) {
        const QString key = deepgramSettings()->apiKey();
        const QString voice = deepgramSettings()->speakVoice();
        if (key.isEmpty())
            return tr("Add your Deepgram API key to use this voice.");
        if (!deepgram::keyLooksValid(key))
            return tr("That does not look like a Deepgram API key.");
        return voice.trimmed().isEmpty() ? tr("Choose a Deepgram voice.") : QString();
    }
    // google (and anything else, which setEngine maps to google)
    return m_googleKey.trimmed().isEmpty() ? tr("Add a Google AI Studio API key to use this voice.") : QString();
}

QVariantMap SpeechOut::engineStatuses() const
{
    QVariantMap out;
    auto entry = [](bool ready, const QString &detail) {
        return QVariantMap{ { QStringLiteral("ready"), ready }, { QStringLiteral("detail"), detail } };
    };
    out[QStringLiteral("google")] = entry(!m_googleKey.trimmed().isEmpty(),
                                          m_googleKey.trimmed().isEmpty()
                                              ? tr("Add a Google AI Studio API key (aistudio.google.com/apikey).")
                                              : tr("Voice %1 with Gemini TTS.").arg(m_googleVoice));
    const QString dgKeyValue = deepgramSettings()->apiKey();
    const QString dgVoice = deepgramSettings()->speakVoice();
    const bool dgKey = deepgram::keyLooksValid(dgKeyValue);
    out[QStringLiteral("deepgram")] = entry(dgKey && !dgVoice.isEmpty(),
                                            dgKeyValue.isEmpty() ? tr("Add your Deepgram API key (console.deepgram.com).")
                                            : !dgKey ? tr("That does not look like a Deepgram API key.")
                                                     : tr("Voice %1 with Deepgram Aura.").arg(deepgram::voiceLabel(dgVoice)));
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
    // Only the two cloud voices remain; anything else (including the retired
    // "system" and "kokoro" values an old settings file may hold) is google.
    const QString e = engine == QLatin1String("deepgram") ? engine : QStringLiteral("google");
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
#undef NOCH_SPEECH_SETTER

void SpeechOut::setDeepgramSettings(DeepgramSettings *settings)
{
    // Drop both possible live connections (the old effective object and the
    // fallback the constructor wired) so the new one is connected exactly once.
    disconnect(deepgramSettings(), &DeepgramSettings::configChanged, this, nullptr);
    m_fallbackSettings.disconnect(this);
    m_settings = settings;
    connect(deepgramSettings(), &DeepgramSettings::configChanged, this, [this] { emit configChanged(); });
    emit configChanged();
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
    return m_synthesizing || m_streaming || !m_stream.idle() || m_player.playing();
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
    // No fallback voice: when the chosen one cannot run, the reason is the
    // answer (problem/error, shown in Settings) and nothing is spoken.
    if (const QString p = problemFor(m_activeEngine); !p.isEmpty()) {
        setError(p);
        return;
    }
    if (m_activeEngine == QLatin1String("deepgram"))
        beginDeepgramStream(text);
    else
        beginPipeline(text);
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
    // Sentences arrive much faster than they are spoken. Keep only a few seconds
    // ahead of the speakers instead of queueing a whole long reply as raw audio.
    constexpr double kMaxAheadSeconds = 12.0;
    if (m_stream.bufferedSeconds() > kMaxAheadSeconds) {
        if (!m_streamWaiting) {
            m_streamWaiting = true;
            const quint64 waitGen = m_generation;
            QTimer::singleShot(250, this, [this, waitGen] {
                m_streamWaiting = false;
                if (waitGen == m_generation)
                    streamNextChunk();
            });
        }
        return;
    }
    m_streamCurrent = m_streamChunks.takeFirst();
    const quint64 gen = m_generation;
    QNetworkRequest request{ deepgram::speakUrl(deepgramSettings()->speakVoice(), 24000, m_speed, m_deepgramBase, QStringLiteral("none")) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", deepgram::authorizationHeader(deepgramSettings()->apiKey()));
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

// The stream failed mid-reply: what was already heard stays heard, the rest is
// not spoken (there is no fallback voice), and the reason is kept in `error`
// where Settings can show it.
void SpeechOut::onStreamFailure(const QString &why)
{
    const bool wasStreaming = m_streaming;
    stopStream();
    ++m_generation;
    if (!wasStreaming)
        return;
    setError(why);
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
// signal advances the pipeline. A chunk that fails ends the reading there:
// the rest is not spoken, and the reason lands in `error`.

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
    QNetworkRequest request{ QUrl(m_googleEndpoint) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
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
    const QJsonObject body = {
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
    m_reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        if (m_reply == reply)
            m_reply = nullptr;   // no request in flight while this answer is handled
        reply->deleteLater();
        if (gen != m_generation || !m_pipeline)
            return;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString why = apiMessage(data);
            if (status == 400 || status == 403 || status == 401)
                why = why.isEmpty() ? tr("Google AI Studio refused the request (HTTP %1). Check the API key.").arg(status)
                                    : tr("Google: %1").arg(why);
            else if (why.isEmpty())
                why = status > 0 ? tr("Gemini TTS answered HTTP %1.").arg(status)
                                 : tr("Could not reach Google AI Studio.");
            onFailure(why, remainingSpeech());
            return;
        }
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
        const QByteArray wav = (bytes.startsWith("RIFF") || audio.mimeType.contains(QLatin1String("wav"), Qt::CaseInsensitive))
                                   ? bytes
                                   : voice::encodeWav(bytes, 24000);
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

// The pipeline failed: stop the cloud work at once, stop what is playing, and
// keep the reason in `error` where the settings can show it. The rest of the
// reply is not spoken — there is no fallback voice.
void SpeechOut::onFailure(const QString &why, const QString &text)
{
    Q_UNUSED(text);
    m_pipeline = false;
    m_synthesizing = false;
    m_chunks.clear();
    m_currentChunk.clear();
    m_bufferedPcm.clear();
    m_bufferedRate = m_bufferedChannels = 0;
    m_player.stop();
    setError(why);
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
    setSpeaking();
}
