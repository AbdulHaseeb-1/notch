// OAuth 2.0 for a desktop app: authorization code + PKCE (RFC 7636) with a loopback
// redirect on 127.0.0.1 (RFC 8252), the flow Google documents for installed apps.
//
// start() makes a one-time code verifier and state, listens on 127.0.0.1 on a free
// port, opens the provider's sign-in page in the user's browser and waits for the
// redirect. The state must match; the code is then exchanged for tokens together with
// the verifier. Nothing is asked of the user but the consent screen: Noch never sees
// the account password. The listener accepts one answer and closes, and also closes
// on timeout or cancel.
//
// Tokens are only data here; where they are kept is the caller's business (see
// SecretStore). Replies are untrusted text: errors are summarised, never shown raw.
#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace oauth {

struct Tokens {
    QString access;
    QString refresh;
    QString scope;
    qint64 expiresAtMs = 0;   // since epoch; 0 = unknown

    bool valid() const { return !access.isEmpty(); }
    bool expiresSoon(qint64 nowMs, int marginMs = 60000) const { return expiresAtMs > 0 && nowMs + marginMs >= expiresAtMs; }
    QJsonObject toJson() const;
    static Tokens fromJson(const QJsonObject &o);
};

// 32 random bytes, base64url without padding (a code verifier, a state).
QString randomUrlSafe(int bytes = 32);
// BASE64URL(SHA256(verifier)), the S256 code challenge.
QString pkceChallenge(const QString &verifier);

struct AuthRequest {
    QUrl endpoint;
    QString clientId;
    QString redirectUri;
    QStringList scopes;
    QString state;
    QString challenge;
    QList<QPair<QString, QString>> extra;   // e.g. access_type=offline
};
QUrl buildAuthUrl(const AuthRequest &req);

// A token endpoint answer. `previous` keeps its refresh token when the reply has none
// (a refresh normally does not repeat it). On failure `error` is set and the result is invalid.
Tokens parseTokenResponse(const QByteArray &body, const Tokens &previous, qint64 nowMs, QString *error);
// "invalid_grant" style code from an error body, or "".
QString errorCode(const QByteArray &body);
QString errorSummary(const QByteArray &body, int status, const QString &transportError);

} // namespace oauth

class OAuthClient : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QUrl authUrl;
        QUrl tokenUrl;
        QString clientId;
        QString clientSecret;   // optional: Google's Desktop clients still want it at the token endpoint
        QStringList scopes;
        QList<QPair<QString, QString>> extraAuthParams;
    };
    // `nam` is not owned and must outlive this object.
    explicit OAuthClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setConfig(const Config &config) { m_cfg = config; }
    const Config &config() const { return m_cfg; }
    // How the sign-in page is opened (default: the user's browser). Tests supply their own.
    using Opener = std::function<bool(const QUrl &)>;
    void setOpener(Opener opener) { m_opener = std::move(opener); }

    bool busy() const { return m_server.isListening(); }
    // Signs in. Answers finished() exactly once unless cancelled.
    void start();
    void cancel();

    // Exchanges a refresh token. `permanent` is true when the grant is gone for good
    // (revoked, expired): the user has to sign in again.
    using RefreshDone = std::function<void(bool ok, const oauth::Tokens &tokens, const QString &error, bool permanent)>;
    void refresh(const oauth::Tokens &tokens, RefreshDone done);

    int signInTimeoutMs = 180000;
    int requestTimeoutMs = 20000;

signals:
    void finished(bool ok, const oauth::Tokens &tokens, const QString &error);

private:
    void onConnection();
    void handleRequest(QTcpSocket *socket, const QByteArray &request);
    void respond(QTcpSocket *socket, int status, const QString &title, const QString &message);
    void exchange(const QString &code);
    void fail(const QString &error);
    void closeServer();
    QNetworkRequest tokenRequest() const;

    QNetworkAccessManager *m_nam;
    Config m_cfg;
    Opener m_opener;
    QTcpServer m_server;
    QTimer m_timeout;
    QString m_verifier;
    QString m_state;
    QString m_redirect;
    QPointer<QNetworkReply> m_exchangeReply;
    bool m_done = true;
};

Q_DECLARE_METATYPE(oauth::Tokens)
