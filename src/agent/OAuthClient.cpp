#include "OAuthClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

#include <memory>

namespace oauth {

QJsonObject Tokens::toJson() const
{
    return QJsonObject{ { QStringLiteral("access"), access },
                        { QStringLiteral("refresh"), refresh },
                        { QStringLiteral("scope"), scope },
                        { QStringLiteral("expiresAtMs"), double(expiresAtMs) } };
}

Tokens Tokens::fromJson(const QJsonObject &o)
{
    Tokens t;
    t.access = o.value(QStringLiteral("access")).toString();
    t.refresh = o.value(QStringLiteral("refresh")).toString();
    t.scope = o.value(QStringLiteral("scope")).toString();
    t.expiresAtMs = qint64(o.value(QStringLiteral("expiresAtMs")).toDouble());
    return t;
}

QString randomUrlSafe(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i)
        raw[i] = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString pkceChallenge(const QString &verifier)
{
    const QByteArray digest = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QUrl buildAuthUrl(const AuthRequest &req)
{
    QUrl url = req.endpoint;
    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("client_id"), req.clientId);
    q.addQueryItem(QStringLiteral("redirect_uri"), req.redirectUri);
    q.addQueryItem(QStringLiteral("scope"), req.scopes.join(QLatin1Char(' ')));
    q.addQueryItem(QStringLiteral("state"), req.state);
    q.addQueryItem(QStringLiteral("code_challenge"), req.challenge);
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    for (const auto &p : req.extra)
        q.addQueryItem(p.first, p.second);
    url.setQuery(q);
    return url;
}

Tokens parseTokenResponse(const QByteArray &body, const Tokens &previous, qint64 nowMs, QString *error)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString access = o.value(QStringLiteral("access_token")).toString();
    if (access.isEmpty()) {
        if (error) {
            const QString desc = o.value(QStringLiteral("error_description")).toString();
            const QString code = o.value(QStringLiteral("error")).toString();
            *error = !desc.isEmpty() ? desc : !code.isEmpty() ? code : QObject::tr("The sign-in service sent no access token.");
        }
        return Tokens();
    }
    Tokens t;
    t.access = access;
    t.refresh = o.value(QStringLiteral("refresh_token")).toString(previous.refresh);
    t.scope = o.value(QStringLiteral("scope")).toString(previous.scope);
    const int secs = o.value(QStringLiteral("expires_in")).toInt(3600);
    t.expiresAtMs = nowMs + qint64(secs) * 1000;
    return t;
}

QString errorCode(const QByteArray &body)
{
    const QJsonValue e = QJsonDocument::fromJson(body).object().value(QStringLiteral("error"));
    return e.isString() ? e.toString() : QString();
}

QString errorSummary(const QByteArray &body, int status, const QString &transportError)
{
    QString msg;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString desc = o.value(QStringLiteral("error_description")).toString().trimmed();
    const QString code = errorCode(body);
    msg = !desc.isEmpty() ? desc : code;
    if (msg.isEmpty()) {
        msg = transportError.trimmed();
        const qsizetype cut = msg.lastIndexOf(QLatin1String(": "));
        if (cut > 0)
            msg = msg.mid(cut + 2).trimmed();
    }
    if (msg.size() > 160)
        msg = msg.left(157) + QStringLiteral("…");
    if (status > 0)
        return msg.isEmpty() ? QObject::tr("HTTP %1").arg(status) : QObject::tr("HTTP %1 — %2").arg(status).arg(msg);
    return msg.isEmpty() ? QObject::tr("No answer from the sign-in service.") : msg;
}

} // namespace oauth

// ---------------------------------------------------------------------------

OAuthClient::OAuthClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    qRegisterMetaType<oauth::Tokens>();
    m_opener = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(tr("Sign-in timed out. Try again."));
    });
    connect(&m_server, &QTcpServer::newConnection, this, &OAuthClient::onConnection);
}

void OAuthClient::closeServer()
{
    m_timeout.stop();
    if (m_server.isListening())
        m_server.close();
}

void OAuthClient::cancel()
{
    const bool wasRunning = !m_done;
    closeServer();
    if (m_exchangeReply) {
        QNetworkReply *r = m_exchangeReply.data();
        m_exchangeReply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_done = true;
    Q_UNUSED(wasRunning);
}

void OAuthClient::fail(const QString &error)
{
    if (m_done)
        return;
    cancel();
    emit finished(false, oauth::Tokens(), error);
}

void OAuthClient::start()
{
    cancel();
    if (m_cfg.clientId.trimmed().isEmpty()) {
        m_done = false;
        QTimer::singleShot(0, this, [this] { fail(tr("Enter your Google OAuth client ID first.")); });
        return;
    }
    // Loopback on the IPv4 address: this is the redirect Google documents for desktop apps.
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        m_done = false;
        QTimer::singleShot(0, this, [this] { fail(tr("Could not listen on this computer for the sign-in answer.")); });
        return;
    }
    m_done = false;
    m_verifier = oauth::randomUrlSafe(48);
    m_state = oauth::randomUrlSafe(24);
    m_redirect = QStringLiteral("http://127.0.0.1:%1/oauth/callback").arg(m_server.serverPort());
    oauth::AuthRequest req;
    req.endpoint = m_cfg.authUrl;
    req.clientId = m_cfg.clientId.trimmed();
    req.redirectUri = m_redirect;
    req.scopes = m_cfg.scopes;
    req.state = m_state;
    req.challenge = oauth::pkceChallenge(m_verifier);
    req.extra = m_cfg.extraAuthParams;
    m_timeout.start(signInTimeoutMs);
    if (!m_opener || !m_opener(oauth::buildAuthUrl(req)))
        QTimer::singleShot(0, this, [this] { fail(tr("Could not open your browser for the sign-in.")); });
}

void OAuthClient::onConnection()
{
    while (QTcpSocket *s = m_server.nextPendingConnection()) {
        auto buf = std::make_shared<QByteArray>();
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
            *buf += s->readAll();
            if (buf->indexOf("\r\n") < 0 && buf->size() < 8192)
                return;
            handleRequest(s, *buf);
            buf->clear();
        });
    }
}

void OAuthClient::handleRequest(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> parts = request.left(request.indexOf("\r\n")).split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        respond(socket, 405, tr("Not found"), tr("This address only receives the sign-in answer."));
        return;
    }
    const QUrl url = QUrl::fromEncoded("http://127.0.0.1" + parts.at(1));
    if (url.path() != QLatin1String("/oauth/callback")) {
        respond(socket, 404, tr("Not found"), tr("This address only receives the sign-in answer."));
        return;
    }
    const QUrlQuery q(url);
    // Anything that is not the answer to this very sign-in is ignored: another page on this
    // computer must not be able to end it or inject a code.
    if (q.queryItemValue(QStringLiteral("state")) != m_state) {
        respond(socket, 400, tr("Sign-in failed"), tr("This answer does not belong to the sign-in Noch started. You can close this tab."));
        return;
    }
    const QString err = q.queryItemValue(QStringLiteral("error"));
    const QString code = q.queryItemValue(QStringLiteral("code"));
    if (!err.isEmpty() || code.isEmpty()) {
        respond(socket, 200, tr("Sign-in cancelled"), tr("Nothing was connected. You can close this tab and go back to Noch."));
        fail(err == QLatin1String("access_denied") ? tr("You declined the access request.")
                                                   : tr("The sign-in was not completed (%1).").arg(err.isEmpty() ? tr("no code") : err.left(60)));
        return;
    }
    respond(socket, 200, tr("Connected"), tr("You can close this tab and go back to Noch."));
    // One answer is all there is: stop listening before the exchange.
    m_server.close();
    exchange(code);
}

void OAuthClient::respond(QTcpSocket *socket, int status, const QString &title, const QString &message)
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    const QByteArray page = QStringLiteral("<!doctype html><meta charset=utf-8><title>%1</title>"
                                           "<body style=\"font-family:sans-serif;max-width:32em;margin:15vh auto;text-align:center\">"
                                           "<h2>%1</h2><p>%2</p></body>").arg(esc(title), esc(message)).toUtf8();
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: text/html; charset=utf-8\r\n"
                  "Cache-Control: no-store\r\nContent-Length: " + QByteArray::number(page.size())
                  + "\r\nConnection: close\r\n\r\n" + page);
    socket->disconnectFromHost();
}

QNetworkRequest OAuthClient::tokenRequest() const
{
    QNetworkRequest req(m_cfg.tokenUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(requestTimeoutMs);
    return req;
}

void OAuthClient::exchange(const QString &code)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("redirect_uri"), m_redirect);
    form.addQueryItem(QStringLiteral("client_id"), m_cfg.clientId.trimmed());
    form.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    if (!m_cfg.clientSecret.trimmed().isEmpty())
        form.addQueryItem(QStringLiteral("client_secret"), m_cfg.clientSecret.trimmed());
    QNetworkReply *reply = m_nam->post(tokenRequest(), form.toString(QUrl::FullyEncoded).toUtf8());
    m_exchangeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_exchangeReply == reply)
            m_exchangeReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString transport = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (m_done)
            return;
        QString err;
        oauth::Tokens t;
        if (ok)
            t = oauth::parseTokenResponse(body, oauth::Tokens(), QDateTime::currentMSecsSinceEpoch(), &err);
        else
            err = oauth::errorSummary(body, status, transport);
        if (!t.valid()) {
            fail(err.isEmpty() ? tr("The sign-in service refused the code.") : err);
            return;
        }
        m_done = true;
        closeServer();
        emit finished(true, t, QString());
    });
}

void OAuthClient::refresh(const oauth::Tokens &tokens, RefreshDone done)
{
    if (tokens.refresh.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done(false, oauth::Tokens(), QObject::tr("No refresh token."), true); });
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), tokens.refresh);
    form.addQueryItem(QStringLiteral("client_id"), m_cfg.clientId.trimmed());
    if (!m_cfg.clientSecret.trimmed().isEmpty())
        form.addQueryItem(QStringLiteral("client_secret"), m_cfg.clientSecret.trimmed());
    QNetworkReply *reply = m_nam->post(tokenRequest(), form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, tokens, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString transport = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (ok) {
            QString err;
            const oauth::Tokens t = oauth::parseTokenResponse(body, tokens, QDateTime::currentMSecsSinceEpoch(), &err);
            if (t.valid()) {
                done(true, t, QString(), false);
                return;
            }
            done(false, oauth::Tokens(), err, false);
            return;
        }
        // A 400/401 with invalid_grant means the user revoked access or the grant expired.
        const QString code = oauth::errorCode(body);
        const bool permanent = code == QLatin1String("invalid_grant") || code == QLatin1String("invalid_client")
                            || code == QLatin1String("unauthorized_client");
        done(false, oauth::Tokens(), oauth::errorSummary(body, status, transport), permanent);
    });
}
