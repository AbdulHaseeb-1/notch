// Gmail for Buddy: search and read mail, write drafts, send.
//
// Sign-in is Google's OAuth 2.0 for desktop apps (PKCE, loopback redirect, see
// OAuthClient.h) with the user's own Google Cloud OAuth client: Noch ships no client
// ID of its own. The scopes are the narrowest that do the job:
//   gmail.readonly   search and read messages and threads
//   gmail.compose    create drafts and send mail (no read-modify, no delete)
// Tokens are kept encrypted (SecretStore), never in plain settings.
//
// Tools: gmail_search_messages, gmail_read_message, gmail_read_thread (read);
//        gmail_create_draft, gmail_send_message (write: asked every time).
// Mail is written by other people. The text the model gets is stripped of invisible
// characters and cut to a sane size, and the gate fences it as untrusted content.
#pragma once

#include <QNetworkAccessManager>
#include <QUrlQuery>

#include "Connector.h"
#include "OAuthClient.h"

class SecretStore;

namespace gmail {

constexpr const char *kAuthUrl = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char *kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr const char *kRevokeUrl = "https://oauth2.googleapis.com/revoke";
constexpr const char *kApiBase = "https://gmail.googleapis.com/gmail/v1";
constexpr const char *kScopeRead = "https://www.googleapis.com/auth/gmail.readonly";
constexpr const char *kScopeCompose = "https://www.googleapis.com/auth/gmail.compose";

constexpr int kMaxBodyChars = 12000;
constexpr int kMaxSearchResults = 20;

struct Message {
    QString id, threadId;
    QString from, to, cc, replyTo, subject, date;
    QString messageId, references;   // RFC 5322 headers, for threading a reply
    QString snippet, body;
    bool unread = false;
    QStringList attachments;         // file names only; attachments are never downloaded
};

// Reads a users.messages resource in either "metadata" or "full" format.
Message parseMessage(const QJsonObject &msg);
QByteArray decodeBase64Url(const QString &data);
// Readable text from HTML mail: scripts and styles dropped, tags stripped, entities decoded.
QString htmlToText(const QString &html);

// "Name <a@b.c>, d@e.f" -> validated, header-safe list. Returns "" on success and sets
// `normalized`; otherwise a sentence for the model saying what is wrong.
QString normalizeAddresses(const QString &list, QString *normalized);
// A header value that cannot break out of its line (CR/LF removed); non-ASCII text is
// encoded as RFC 2047 words.
QString encodeHeaderValue(const QString &value);

struct Outgoing {
    QString to, cc, bcc, subject, body;
    QString inReplyTo, references;   // set for a reply
};
// The RFC 5322 message for drafts.create / messages.send (CRLF, UTF-8, base64 body).
QByteArray buildRaw(const Outgoing &mail);

// Text for the model.
QString renderMessage(const Message &m, int maxBody = kMaxBodyChars);
QString renderListLine(int index, const Message &m);

QList<connectors::ToolDef> toolDefs();

} // namespace gmail

class GmailConnector : public Connector
{
    Q_OBJECT
public:
    explicit GmailConnector(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("gmail"); }
    QString name() const override { return tr("Gmail"); }
    QString summary() const override { return tr("Search and read your mail, write drafts and send messages."); }
    QString icon() const override { return QStringLiteral("mail"); }
    QString kind() const override { return QStringLiteral("oauth"); }
    QList<connectors::Permission> permissions() const override;
    QList<connectors::ToolDef> tools() const override;
    bool configured() const override { return !m_clientId.isEmpty(); }

    void connectAccount() override;
    void disconnectAccount() override;
    void call(const QString &tool, const QJsonObject &args, connectors::Done done) override;
    void cancelAll() override;

    // The user's own Google OAuth client (Settings > Connectors). The secret is optional
    // (Google's Desktop clients have one that is not confidential) and is kept encrypted.
    void setClientCredentials(const QString &clientId, const QString &clientSecret);
    QString clientId() const { return m_clientId; }
    bool hasClientSecret() const { return !m_clientSecret.isEmpty(); }
    void setSecretStore(SecretStore *store) { m_secrets = store; }
    // Reads saved tokens: a connector with a refresh token is connected without asking again.
    void restore();

    // Tests point the connector at fake servers.
    void setEndpoints(const QUrl &auth, const QUrl &token, const QUrl &revoke, const QString &apiBase);
    void setOpener(OAuthClient::Opener opener) { m_oauth.setOpener(std::move(opener)); }
    OAuthClient *oauthClient() { return &m_oauth; }
    int requestTimeoutMs = 30000;

private:
    struct Api {
        bool ok = false;
        int status = 0;
        QJsonObject json;
        QString error;
    };
    using ApiDone = std::function<void(const Api &)>;

    void api(const QByteArray &verb, const QString &path, const QUrlQuery &query, const QJsonObject &body,
             ApiDone done, bool retried = false);
    void withToken(std::function<void(bool ok, const QString &error)> cont);
    void onSignedIn(bool ok, const oauth::Tokens &tokens, const QString &error);
    void saveTokens();
    void forget(const QString &why);
    QString errorText(const Api &a) const;

    void search(const QJsonObject &args, connectors::Done done);
    void readMessage(const QJsonObject &args, connectors::Done done);
    void readThread(const QJsonObject &args, connectors::Done done);
    void compose(const QJsonObject &args, bool send, connectors::Done done);
    void finishCompose(gmail::Outgoing mail, const QString &threadId, bool send, connectors::Done done);

    QNetworkAccessManager m_nam;
    OAuthClient m_oauth;
    SecretStore *m_secrets = nullptr;
    QString m_clientId, m_clientSecret;
    oauth::Tokens m_tokens;
    QString m_apiBase;
    QUrl m_revokeUrl;
    bool m_refreshing = false;
    QList<std::function<void(bool, const QString &)>> m_refreshWaiters;
    quint64 m_epoch = 0;   // bumped by disconnect/cancel so late answers are dropped
};
