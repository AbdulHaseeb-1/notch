// OAuth 2.0 Device Authorization Grant (RFC 8628) for a desktop app — the flow
// GitHub documents for its OAuth Apps and the one the GitHub CLI uses.
//
// start() asks the provider for a device code, opens the provider's verification
// page in the user's browser and reports the short user code through
// codeAvailable() (the settings page shows it, in case the browser did not come to
// the front). The client then polls the token endpoint at the provider's interval:
// `authorization_pending` keeps polling, `slow_down` lengthens the wait by five
// seconds (or to the interval the answer carries), `access_denied` and
// `expired_token` end the sign-in. No client secret is involved anywhere: for the
// device flow there is none to send (GitHub's error table says the client_secret
// "is not needed for the device flow").
//
// Tokens are only data here; where they are kept is the caller's business (see
// SecretStore). Answers are untrusted text: errors are summarised, never shown raw.
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <functional>

#include "OAuthClient.h"   // oauth::Tokens and its response parser

class QNetworkReply;

class DeviceFlowClient : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QUrl deviceCodeUrl;   // e.g. https://github.com/login/device/code
        QUrl tokenUrl;        // e.g. https://github.com/login/oauth/access_token
        QString clientId;     // the app's own public client ID (BuiltinOAuth)
        QStringList scopes;
    };
    // `nam` is not owned and must outlive this object.
    explicit DeviceFlowClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setConfig(const Config &config) { m_cfg = config; }
    const Config &config() const { return m_cfg; }
    // How the verification page is opened (default: the user's browser). Tests supply their own.
    using Opener = std::function<bool(const QUrl &)>;
    void setOpener(Opener opener) { m_opener = std::move(opener); }

    bool busy() const { return m_running; }
    // Signs in. Answers finished() exactly once unless cancelled.
    void start();
    void cancel();

    int requestTimeoutMs = 20000;
    // Tests shrink the provider's polling interval; 0 = use the interval the provider sent.
    int pollIntervalOverrideMs = 0;

signals:
    // The code the user types on the verification page (already open in the browser).
    void codeAvailable(const QString &userCode, const QUrl &verificationUri);
    void finished(bool ok, const oauth::Tokens &tokens, const QString &error);

private:
    void requestDeviceCode();
    void poll();
    void schedulePoll();
    void fail(const QString &error);
    QNetworkRequest formRequest(const QUrl &url) const;

    QNetworkAccessManager *m_nam;
    Config m_cfg;
    Opener m_opener;
    QTimer m_pollTimer;
    QPointer<QNetworkReply> m_reply;
    QString m_deviceCode;
    int m_intervalSec = 5;
    qint64 m_deadlineMs = 0;
    bool m_running = false;
    bool m_done = true;
};
