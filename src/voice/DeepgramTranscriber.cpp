#include "Transcriber.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>

#include "DeepgramProtocol.h"
#include "VoiceAudio.h"

namespace {

// Speech to text through Deepgram's pre-recorded endpoint: one POST per
// utterance. A short utterance (a few seconds) comes back in a fraction of a
// second, and the connection is kept warm so the TLS handshake is not paid
// on every request.
class DeepgramTranscriber final : public QObject, public Transcriber
{
public:
    explicit DeepgramTranscriber(const DeepgramListenSettings &s)
        : m_settings(s)
    {
        m_settings.apiKey = m_settings.apiKey.trimmed();
        if (m_settings.apiBase.isEmpty())
            m_settings.apiBase = QString::fromLatin1(deepgram::kApiBase);
        // Soonest first connection: resolve and handshake while the user is still talking.
        if (QUrl(m_settings.apiBase).scheme() == QLatin1String("https") && deepgram::keyLooksValid(m_settings.apiKey))
            m_nam.connectToHostEncrypted(QUrl(m_settings.apiBase).host());
    }

    QString name() const override { return QStringLiteral("Deepgram %1").arg(m_settings.model); }
    bool isAsync() const override { return true; }

    QString problem() const override
    {
        if (m_settings.apiKey.isEmpty())
            return QObject::tr("Add your Deepgram API key to use it for speech to text.");
        if (!deepgram::keyLooksValid(m_settings.apiKey))
            return QObject::tr("That does not look like a Deepgram API key.");
        if (m_settings.model.trimmed().isEmpty())
            return QObject::tr("Choose a Deepgram speech-to-text model.");
        return QString();
    }

    QString prepare() override { return problem(); }

    // The synchronous path is only here to satisfy the interface; VoiceInput uses transcribeAsync().
    QString transcribe(const QByteArray &, QString *error) override
    {
        if (error)
            *error = QObject::tr("Deepgram speech to text runs asynchronously.");
        return QString();
    }

    void transcribeAsync(const QByteArray &pcm16,
                         std::function<void(const QString &text, const QString &error)> done) override
    {
        if (const QString p = problem(); !p.isEmpty()) {
            done(QString(), p);
            return;
        }
        // Flux models stream only; the pre-recorded endpoint takes the nova family.
        const QString model = deepgram::isFluxModel(m_settings.model) ? QStringLiteral("nova-3") : m_settings.model.trimmed();
        QNetworkRequest req(deepgram::listenUrl(model, m_settings.language, true, m_settings.keyterms, m_settings.apiBase));
        req.setRawHeader("Authorization", deepgram::authorizationHeader(m_settings.apiKey));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
        // A hung connection must not leave Buddy "transcribing" forever.
        req.setTransferTimeout(20000);
        QNetworkReply *reply = m_nam.post(req, voice::encodeWav(voice::conditionForStt(pcm16)));
        connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
            reply->deleteLater();
            const QByteArray body = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
                done(deepgram::transcriptFromListen(body), QString());
                return;
            }
            QString why = deepgram::restErrorMessage(body);
            switch (status) {
            case 401:
            case 403: why = QObject::tr("Deepgram rejected the API key."); break;
            case 402: why = QObject::tr("Your Deepgram account is out of credit."); break;
            case 429: why = QObject::tr("Deepgram is limiting requests right now. Try again in a moment."); break;
            default:
                if (why.isEmpty()) {
                    why = status > 0 ? QObject::tr("Deepgram answered HTTP %1.").arg(status)
                          : reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError
                              ? QObject::tr("Deepgram did not answer in time.")
                              : QObject::tr("Could not reach Deepgram. Check your internet connection.");
                }
            }
            done(QString(), why);
        });
    }

private:
    DeepgramListenSettings m_settings;
    QNetworkAccessManager m_nam;
};

} // namespace

std::unique_ptr<Transcriber> makeDeepgramTranscriber(const DeepgramListenSettings &settings)
{
    return std::make_unique<DeepgramTranscriber>(settings);
}
