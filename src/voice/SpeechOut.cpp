#include "SpeechOut.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

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

QString SpeechOut::googleLanguage(const QString &voice)
{
    const QStringList parts = voice.trimmed().split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return QStringLiteral("en-US");
    return parts.at(0).toLower() + QLatin1Char('-') + parts.at(1).toUpper();
}

QString SpeechOut::problem() const
{
    if (m_engine == QLatin1String("google"))
        return m_googleKey.trimmed().isEmpty() ? tr("Add a Google Cloud API key (Text-to-Speech API) to use this voice.") : QString();
    if (m_engine == QLatin1String("kokoro")) {
        if (const QString p = kokoroAddressProblem(m_kokoroBase); !p.isEmpty())
            return p;
        return m_kokoroVoice.trimmed().isEmpty() ? tr("Add a Kokoro voice name, for example af_heart.") : QString();
    }
    return m_program.isEmpty() ? tr("No voice is installed on this computer.") : QString();
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
    const QString e = (engine == QLatin1String("google") || engine == QLatin1String("kokoro")) ? engine : QStringLiteral("system");
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
NOCH_SPEECH_SETTER(setGoogleVoice, m_googleVoice, value.trimmed())
NOCH_SPEECH_SETTER(setKokoroBase, m_kokoroBase, value.trimmed())
NOCH_SPEECH_SETTER(setKokoroVoice, m_kokoroVoice, value.trimmed())
#undef NOCH_SPEECH_SETTER

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
    return m_synthesizing || m_player.playing() || m_proc.state() != QProcess::NotRunning;
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

void SpeechOut::start(const QString &text, bool force)
{
    if (!force && !m_enabled)
        return;
    stop();
    setError(QString());
    ++m_generation;
    if (m_engine == QLatin1String("google") && !m_googleKey.trimmed().isEmpty()) {
        startGoogle(text);
    } else if (m_engine == QLatin1String("kokoro") && problem().isEmpty()) {
        startKokoro(text);
    } else {
        if (m_engine != QLatin1String("system"))
            setError(problem() + QLatin1Char(' ') + tr("Using the system voice."));
        startSystem(text);
    }
}

void SpeechOut::startSystem(const QString &text)
{
    if (m_program.isEmpty())
        return;
    m_proc.setProgram(m_program);
    m_proc.setArguments(m_args);
    m_proc.start();
    setSpeaking();   // starting counts as speaking: the listener must not hear it
    // Written once and closed: the program reads until the end of its input.
    m_proc.write(text.toUtf8());
    m_proc.closeWriteChannel();
}

void SpeechOut::startGoogle(const QString &text)
{
    QNetworkRequest request{ QUrl(m_googleEndpoint) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // The key goes in a header, not the URL, so it does not end up in any log of addresses.
    request.setRawHeader("X-Goog-Api-Key", m_googleKey.trimmed().toUtf8());
    request.setTransferTimeout(30000);
    QJsonObject audio{ { QStringLiteral("audioEncoding"), QStringLiteral("LINEAR16") },
                       { QStringLiteral("sampleRateHertz"), 24000 } };
    if (!qFuzzyCompare(m_speed, 1.0))
        audio[QStringLiteral("speakingRate")] = m_speed;
    const QJsonObject body{
        { QStringLiteral("input"), QJsonObject{ { QStringLiteral("text"), text } } },
        { QStringLiteral("voice"), QJsonObject{ { QStringLiteral("languageCode"), googleLanguage(m_googleVoice) },
                                                  { QStringLiteral("name"), m_googleVoice } } },
        { QStringLiteral("audioConfig"), audio },
    };
    m_synthesizing = true;
    m_reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const quint64 gen = m_generation;
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, text] {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        m_synthesizing = false;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString why = apiMessage(data);
            if (status == 400 || status == 403 || status == 401)
                why = why.isEmpty() ? tr("Google refused the request (HTTP %1). Check the API key and that the Text-to-Speech API is enabled.").arg(status)
                                    : tr("Google: %1").arg(why);
            else if (why.isEmpty())
                why = status > 0 ? tr("Google Cloud Text-to-Speech answered HTTP %1.").arg(status)
                                 : tr("Could not reach Google Cloud Text-to-Speech.");
            onFailure(why, text);
            return;
        }
        const QByteArray wav = QByteArray::fromBase64(
            QJsonDocument::fromJson(data).object().value(QStringLiteral("audioContent")).toString().toLatin1());
        onAudio(wav, text);
    });
    setSpeaking();
}

void SpeechOut::startKokoro(const QString &text)
{
    QNetworkRequest request{ QUrl(kokoroUrl(m_kokoroBase)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(60000);
    const QJsonObject body{
        { QStringLiteral("model"), QStringLiteral("kokoro") },
        { QStringLiteral("input"), text },
        { QStringLiteral("voice"), m_kokoroVoice },
        { QStringLiteral("response_format"), QStringLiteral("wav") },
        { QStringLiteral("speed"), m_speed },
    };
    m_synthesizing = true;
    m_reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const quint64 gen = m_generation;
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, text] {
        reply->deleteLater();
        if (gen != m_generation)
            return;
        m_synthesizing = false;
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString why = apiMessage(data);
            if (why.isEmpty())
                why = status > 0 ? tr("The Kokoro server answered HTTP %1.").arg(status)
                                 : tr("Could not reach the Kokoro server. Is it running?");
            onFailure(why, text);
            return;
        }
        onAudio(data, text);
    });
    setSpeaking();
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
    if (!m_player.play(pcm, rate, channels)) {
        onFailure(tr("The sound could not be played."), text);
        return;
    }
    setSpeaking();
}

// A voice that failed must not mean a reply that is never heard: say it with the
// system voice and keep the reason where the settings can show it.
void SpeechOut::onFailure(const QString &why, const QString &text)
{
    setError(why + QLatin1Char(' ') + (m_program.isEmpty() ? QString() : tr("Using the system voice.")));
    startSystem(text);
    setSpeaking();
}

void SpeechOut::stop()
{
    ++m_generation;       // a reply still on its way is dropped
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
