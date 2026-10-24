#include "DeviceFlowClient.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {

// A token-endpoint answer as fields, whether the provider sent JSON (GitHub does
// when asked with Accept: application/json) or a form-encoded body.
QHash<QString, QString> answerFields(const QByteArray &body)
{
    QHash<QString, QString> out;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (!o.isEmpty()) {
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.value().isString())
                out.insert(it.key(), it.value().toString());
            else if (it.value().isDouble())
                out.insert(it.key(), QString::number(it.value().toDouble(), 'f', 0));
        }
        return out;
    }
    const QUrlQuery q(QString::fromUtf8(body));
    for (const auto &p : q.queryItems(QUrl::FullyDecoded))
        out.insert(p.first, p.second);
    return out;
}

oauth::Tokens tokensFromFields(const QHash<QString, QString> &f, qint64 nowMs)
{
    oauth::Tokens t;
    t.access = f.value(QStringLiteral("access_token"));
    t.refresh = f.value(QStringLiteral("refresh_token"));
    t.scope = f.value(QStringLiteral("scope"));
    // No expires_in means the token does not expire (GitHub OAuth Apps without
    // token expiration): leave 0 instead of inventing an hour like the JSON
    // parser's default would.
    if (f.contains(QStringLiteral("expires_in")))
        t.expiresAtMs = nowMs + qint64(f.value(QStringLiteral("expires_in")).toDouble()) * 1000;
    return t;
}

} // namespace

DeviceFlowClient::DeviceFlowClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    m_opener = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceFlowClient::poll);
}

QNetworkRequest DeviceFlowClient::formRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(requestTimeoutMs);
    return req;
}

void DeviceFlowClient::cancel()
{
    m_pollTimer.stop();
    if (m_reply) {
        QNetworkReply *r = m_reply.data();
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_running = false;
    m_done = true;
}

void DeviceFlowClient::fail(const QString &error)
{
    if (m_done)
        return;
    cancel();
    emit finished(false, oauth::Tokens(), error);
}

void DeviceFlowClient::start()
{
    cancel();
    m_done = false;
    if (m_cfg.clientId.trimmed().isEmpty()) {
        QTimer::singleShot(0, this, [this] { fail(tr("No OAuth client ID is configured for this sign-in.")); });
        return;
    }
    m_running = true;
    requestDeviceCode();
}

void DeviceFlowClient::requestDeviceCode()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_cfg.clientId.trimmed());
    if (!m_cfg.scopes.isEmpty())
        form.addQueryItem(QStringLiteral("scope"), m_cfg.scopes.join(QLatin1Char(' ')));
    QNetworkReply *reply = m_nam->post(formRequest(m_cfg.deviceCodeUrl), form.toString(QUrl::FullyEncoded).toUtf8());
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reply == reply)
            m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString transport = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (m_done)
            return;
        const QHash<QString, QString> f = answerFields(body);
        m_deviceCode = f.value(QStringLiteral("device_code"));
        const QString userCode = f.value(QStringLiteral("user_code"));
        QUrl verify(f.value(QStringLiteral("verification_uri")));
        if (verify.isEmpty())
            verify = QUrl(f.value(QStringLiteral("verification_url")));
        if (!ok || m_deviceCode.isEmpty() || userCode.isEmpty() || !verify.isValid()) {
            fail(ok ? tr("The sign-in service sent an incomplete device code answer.")
                    : oauth::errorSummary(body, status, transport));
            return;
        }
        bool numOk = false;
        const double interval = f.value(QStringLiteral("interval")).toDouble(&numOk);
        m_intervalSec = qMax(1, int(numOk ? interval : 5));
        const double expires = f.value(QStringLiteral("expires_in")).toDouble(&numOk);
        const int expiresIn = int(numOk ? expires : 900);
        m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + qint64(expiresIn) * 1000;
        emit codeAvailable(userCode, verify);
        if (m_opener && !m_opener(verify))
            fail(tr("Could not open your browser for the sign-in."));
        schedulePoll();
    });
}

void DeviceFlowClient::schedulePoll()
{
    if (m_done)
        return;
    const int waitMs = pollIntervalOverrideMs > 0 ? pollIntervalOverrideMs : m_intervalSec * 1000;
    m_pollTimer.start(qMax(10, waitMs));
}

void DeviceFlowClient::poll()
{
    if (m_done)
        return;
    if (QDateTime::currentMSecsSinceEpoch() >= m_deadlineMs) {
        fail(tr("The sign-in code expired. Try again."));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_cfg.clientId.trimmed());
    form.addQueryItem(QStringLiteral("device_code"), m_deviceCode);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    QNetworkReply *reply = m_nam->post(formRequest(m_cfg.tokenUrl), form.toString(QUrl::FullyEncoded).toUtf8());
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reply == reply)
            m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString transport = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (m_done)
            return;
        const QHash<QString, QString> f = answerFields(body);
        if (!f.value(QStringLiteral("access_token")).isEmpty()) {
            const oauth::Tokens t = tokensFromFields(f, QDateTime::currentMSecsSinceEpoch());
            cancel();
            m_done = true;
            emit finished(true, t, QString());
            return;
        }
        const QString error = f.value(QStringLiteral("error"));
        if (error == QLatin1String("authorization_pending")) {
            schedulePoll();   // the user has not finished in the browser yet
            return;
        }
        if (error == QLatin1String("slow_down")) {
            // RFC 8628: add five seconds — unless the answer carries the new interval.
            m_intervalSec = f.contains(QStringLiteral("interval"))
                ? qMax(m_intervalSec + 1, int(f.value(QStringLiteral("interval")).toDouble()))
                : m_intervalSec + 5;
            schedulePoll();
            return;
        }
        if (error == QLatin1String("access_denied")) {
            fail(tr("You declined the access request."));
            return;
        }
        if (error == QLatin1String("expired_token")) {
            fail(tr("The sign-in code expired. Try again."));
            return;
        }
        if (error == QLatin1String("device_flow_disabled")) {
            fail(tr("Device flow is not enabled for this app. (Developer step: tick “Enable Device Flow” on the GitHub OAuth App.)"));
            return;
        }
        fail(error.isEmpty() ? oauth::errorSummary(body, status, transport)
                             : tr("The sign-in failed (%1).").arg(error.left(60)));
    });
}
