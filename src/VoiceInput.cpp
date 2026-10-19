#include "VoiceInput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkRequest>
#include <QUrl>

#include "VoiceAudio.h"

// ---- the real microphone -------------------------------------------------------------

namespace {

class MicrophoneCapture final : public AudioCapture
{
public:
    ~MicrophoneCapture() override { stop(); }

    bool start(QString *error) override
    {
        const QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            *error = QObject::tr("No microphone was found.");
            return false;
        }
        QAudioFormat want;
        want.setSampleRate(voice::kSampleRate);
        want.setChannelCount(1);
        want.setSampleFormat(QAudioFormat::Int16);
        // Most microphones can record this directly; if not, take what the device
        // prefers and convert as the audio arrives.
        m_format = device.isFormatSupported(want) ? want : device.preferredFormat();
        m_frameBytes = m_format.bytesPerFrame();
        if (m_frameBytes <= 0) {
            *error = QObject::tr("The microphone's format is not supported.");
            return false;
        }
        m_source = new QAudioSource(device, m_format, this);
        m_io = m_source->start();
        if (!m_io || m_source->error() != QAudio::NoError) {
            *error = QObject::tr("The microphone could not be opened. Check Windows Settings > Privacy > Microphone.");
            stop();
            return false;
        }
        connect(m_io, &QIODevice::readyRead, this, [this] { drain(); });
        return true;
    }

    void stop() override
    {
        drain();   // the last few milliseconds before the key came up are part of the last word
        if (m_source) {
            m_source->stop();
            m_source->deleteLater();
            m_source = nullptr;
        }
        m_io = nullptr;
        m_pending.clear();
    }

private:
    void drain()
    {
        if (!m_io || m_frameBytes <= 0)
            return;
        m_pending += m_io->readAll();
        // Only whole frames; a frame split across two reads waits for its other half.
        const qsizetype whole = (m_pending.size() / m_frameBytes) * m_frameBytes;
        if (whole <= 0)
            return;
        const QByteArray chunk = voice::toMono16k(m_pending.left(whole), m_format);
        m_pending.remove(0, whole);
        if (!chunk.isEmpty())
            emit audio(chunk);
    }

    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    int m_frameBytes = 0;
    QByteArray m_pending;
};

} // namespace

std::unique_ptr<AudioCapture> makeMicrophoneCapture()
{
    return std::make_unique<MicrophoneCapture>();
}

// ---- VoiceInput -------------------------------------------------------------------------

VoiceInput::VoiceInput(QObject *parent)
    : QObject(parent)
{
    m_maxTimer.setSingleShot(true);
    connect(&m_maxTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Listening)
            endListening();   // the maximum: stop and use what was said
    });
}

VoiceInput::~VoiceInput()
{
    if (m_capture)
        m_capture->stop();
}

void VoiceInput::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        cancel();   // turning it off closes the microphone at once
    emit configChanged();
}

void VoiceInput::setApiBase(const QString &b)
{
    if (b == m_apiBase)
        return;
    m_apiBase = b;
    emit configChanged();
}

void VoiceInput::setApiKey(const QString &k)
{
    if (k == m_apiKey)
        return;
    m_apiKey = k;
    emit configChanged();
}

void VoiceInput::setModel(const QString &m)
{
    if (m == m_model)
        return;
    m_model = m;
    emit configChanged();
}

void VoiceInput::setLanguage(const QString &l)
{
    if (l == m_language)
        return;
    m_language = l;
    emit configChanged();
}

bool VoiceInput::isLocalAddress(const QString &base)
{
    const QString host = QUrl::fromUserInput(base).host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")
        || host == QLatin1String("[::1]");
}

QString VoiceInput::addressProblem() const
{
    const QString base = m_apiBase.trimmed();
    if (base.isEmpty())
        return tr("Add the address of a speech-to-text service.");
    const QUrl u = QUrl::fromUserInput(base);
    if (!u.isValid() || u.host().isEmpty())
        return tr("That address does not look right.");
    if (u.scheme() == QLatin1String("https"))
        return QString();
    if (u.scheme() == QLatin1String("http") && isLocalAddress(base))
        return QString();
    return tr("Your voice is only sent over https, or to this computer.");
}

bool VoiceInput::configured() const
{
    if (!addressProblem().isEmpty() || m_model.trimmed().isEmpty())
        return false;
    return !m_apiKey.trimmed().isEmpty() || isLocalAddress(m_apiBase);
}

QString VoiceInput::transcriptionUrl() const
{
    if (!addressProblem().isEmpty())
        return QString();
    QString base = QUrl::fromUserInput(m_apiBase.trimmed()).toString();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/audio/transcriptions")))
        return base;
    if (base.endsWith(QLatin1String("/v1")))
        return base + QStringLiteral("/audio/transcriptions");
    return base + QStringLiteral("/v1/audio/transcriptions");
}

QString VoiceInput::state() const
{
    switch (m_state) {
    case State::Listening: return QStringLiteral("listening");
    case State::Transcribing: return QStringLiteral("transcribing");
    case State::Idle: break;
    }
    return QStringLiteral("idle");
}

double VoiceInput::seconds() const
{
    return voice::seconds(m_pcm);
}

void VoiceInput::setCapture(std::unique_ptr<AudioCapture> capture)
{
    if (m_capture)
        m_capture->stop();
    m_capture = std::move(capture);
}

void VoiceInput::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
}

void VoiceInput::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    emit failed(message);
}

void VoiceInput::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

bool VoiceInput::beginListening()
{
    if (m_state != State::Idle)
        return false;
    clearError();
    if (!m_enabled) {
        fail(tr("Voice input is off. Turn it on in Settings > Buddy."));
        return false;
    }
    if (!configured()) {
        const QString problem = addressProblem();
        fail(problem.isEmpty() ? tr("Add a speech-to-text key in Settings > Buddy.") : problem);
        return false;
    }
    if (!m_capture)
        m_capture = makeMicrophoneCapture();
    QString why;
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &AudioCapture::audio, this, &VoiceInput::onAudio);
    connect(m_capture.get(), &AudioCapture::failed, this, [this](const QString &message) {
        if (m_state != State::Listening)
            return;
        m_capture->stop();
        m_maxTimer.stop();
        m_pcm.clear();
        m_level = 0;
        emit levelChanged();
        setState(State::Idle);
        fail(message);
    });
    m_pcm.clear();
    m_level = 0;
    if (!m_capture->start(&why)) {
        fail(why.isEmpty() ? tr("The microphone could not be opened.") : why);
        return false;
    }
    setState(State::Listening);
    emit levelChanged();
    m_maxTimer.start(m_maxSeconds * 1000);
    return true;
}

void VoiceInput::onAudio(const QByteArray &pcm)
{
    if (m_state != State::Listening)
        return;
    m_pcm += pcm;
    // Rise fast, fall slowly: a meter that follows the voice without flickering.
    m_level = qMax(voice::level(pcm), m_level * 0.82);
    emit levelChanged();
}

void VoiceInput::endListening()
{
    if (m_state != State::Listening)
        return;
    m_maxTimer.stop();
    if (m_capture)
        m_capture->stop();
    m_level = 0;
    emit levelChanged();
    const QByteArray clip = m_pcm;
    m_pcm.clear();
    if (voice::seconds(clip) < kMinSeconds) {
        setState(State::Idle);
        fail(tr("That was too short. Hold the key while you speak."));
        return;
    }
    if (voice::isSilent(clip)) {
        setState(State::Idle);
        fail(tr("I did not hear anything. Check your microphone."));
        return;
    }
    setState(State::Transcribing);
    send(voice::encodeWav(voice::trimSilence(clip)));
}

void VoiceInput::cancel()
{
    ++m_generation;
    m_maxTimer.stop();
    if (m_capture && m_state == State::Listening)
        m_capture->stop();
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_pcm.clear();
    if (m_level != 0) {
        m_level = 0;
        emit levelChanged();
    }
    setState(State::Idle);
}

QNetworkReply *VoiceInput::post(const QByteArray &wav)
{
    QNetworkRequest request{ QUrl(transcriptionUrl()) };
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto field = [multi](const QString &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multi->append(part);
    };
    field(QStringLiteral("model"), m_model.trimmed());
    field(QStringLiteral("response_format"), QStringLiteral("json"));
    field(QStringLiteral("temperature"), QStringLiteral("0"));
    if (!m_language.trimmed().isEmpty())
        field(QStringLiteral("language"), m_language.trimmed());
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"speech.wav\""));
    file.setBody(wav);
    multi->append(file);

    QNetworkReply *reply = m_nam.post(request, multi);
    multi->setParent(reply);   // deleted with the reply
    return reply;
}

VoiceInput::Transcription VoiceInput::parse(QNetworkReply *reply)
{
    Transcription out;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.status = status;
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();   // an abandoned request is closed
    const bool transportOk = reply->error() == QNetworkReply::NoError;

    if (!transportOk || status < 200 || status >= 300) {
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        QString msg = o.value(QStringLiteral("error")).isObject()
            ? o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()
            : o.value(QStringLiteral("error")).toString();
        if (status == 401 || status == 403)
            msg = tr("The speech-to-text key was refused.");
        else if (status == 429)
            msg = tr("The speech-to-text service is busy or out of quota. Try again in a moment.");
        else if (msg.isEmpty())
            msg = status > 0 ? tr("The speech-to-text service answered HTTP %1.").arg(status)
                             : tr("Could not reach the speech-to-text service.");
        out.error = msg.left(300);
        return out;
    }
    // {"text": "..."} (json); some servers answer plain text.
    QString text = QJsonDocument::fromJson(body).object().value(QStringLiteral("text")).toString();
    if (text.isEmpty() && !body.trimmed().startsWith('{'))
        text = QString::fromUtf8(body);
    text = text.simplified();
    if (text.isEmpty()) {
        out.error = tr("I could not make out any words.");
        out.empty = true;
        return out;
    }
    out.ok = true;
    out.text = text;
    return out;
}

void VoiceInput::send(const QByteArray &wav)
{
    m_reply = post(wav);
    const quint64 gen = m_generation;
    connect(m_reply, &QNetworkReply::finished, this, [this, gen] {
        if (gen == m_generation)
            onReply();
    });
}

void VoiceInput::onReply()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();
    const Transcription t = parse(reply);
    setState(State::Idle);
    if (!t.ok) {
        fail(t.error);
        return;
    }
    clearError();
    emit transcript(t.text);
}

QPointer<QNetworkReply> VoiceInput::transcribe(const QByteArray &wav, QObject *context,
                                               std::function<void(const Transcription &)> done)
{
    QNetworkReply *reply = post(wav);
    connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        reply->deleteLater();
        done(parse(reply));
    });
    return reply;
}

bool VoiceInput::localService() const
{
    return isLocalAddress(m_apiBase);
}
