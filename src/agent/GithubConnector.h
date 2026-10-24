// GitHub for Buddy: look at repositories, issues and notifications, open issues
// and comment — after a browser sign-in, nothing to paste.
//
// Sign-in is GitHub's OAuth device flow (RFC 8628, see DeviceFlowClient): Connect
// opens github.com/login/device in the browser, the user approves there, and Noch
// polls until GitHub answers. The only credential the flow uses is Noch's own
// public OAuth client ID (BuiltinOAuth) — GitHub needs no client secret for the
// device flow, and the user never pastes a token. Scopes: `repo` (read and write
// issues in the repositories the user can reach, private ones included) and
// `read:user` (the account name). Tokens are kept encrypted (SecretStore), never
// in plain settings. GitHub offers no way to revoke an OAuth App token without
// the app secret a public client does not have, so Disconnect forgets the tokens
// here; the grant itself can be removed at github.com/settings/applications.
//
// Tools: github_list_repos, github_list_issues, github_read_issue,
//        github_search_issues, github_list_notifications (read);
//        github_create_issue, github_add_comment (write: asked every time).
// What GitHub returns is written by other people: it is stripped of invisible
// characters, cut to a sane size, and fenced as untrusted content by the gate.
#pragma once

#include <QNetworkAccessManager>
#include <QUrlQuery>

#include "Connector.h"
#include "DeviceFlowClient.h"
#include "OAuthClient.h"

class SecretStore;

namespace github {

constexpr const char *kDeviceCodeUrl = "https://github.com/login/device/code";
constexpr const char *kTokenUrl = "https://github.com/login/oauth/access_token";
constexpr const char *kApiBase = "https://api.github.com";

constexpr int kMaxListResults = 30;
constexpr int kMaxBodyChars = 6000;

struct Repo {
    QString fullName, description, language;
    int stars = 0;
    bool isPrivate = false;
    QString updatedAt;
};
struct Issue {
    int number = 0;
    QString title, state, author, repo;   // repo is "owner/name" when the endpoint gives it
    QStringList labels;
    int comments = 0;
    QString createdAt, url, body;
    bool pullRequest = false;
};

Repo parseRepo(const QJsonObject &o);
Issue parseIssue(const QJsonObject &o);
QString renderRepoLine(const Repo &r);
QString renderIssueLine(const Issue &i);
// "owner/name", exactly two non-empty parts without spaces; "" when malformed.
QString checkRepoSlug(const QString &repo);

QList<connectors::ToolDef> toolDefs();

} // namespace github

class GithubConnector : public Connector
{
    Q_OBJECT
public:
    explicit GithubConnector(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("github"); }
    QString name() const override { return tr("GitHub"); }
    QString summary() const override { return tr("See your repositories, issues and notifications; open issues and comment."); }
    QString icon() const override { return QStringLiteral("code"); }
    QString kind() const override { return QStringLiteral("oauth"); }
    QList<connectors::Permission> permissions() const override;
    QList<connectors::ToolDef> tools() const override;
    bool configured() const override { return !m_clientId.isEmpty(); }

    void connectAccount() override;
    void disconnectAccount() override;
    void call(const QString &tool, const QJsonObject &args, connectors::Done done) override;
    void cancelAll() override;

    // Noch's own built-in client ID (BuiltinOAuth). setClientId exists for tests.
    void setClientId(const QString &clientId);
    QString clientId() const { return m_clientId; }
    void setSecretStore(SecretStore *store) { m_secrets = store; }
    // Reads saved tokens: a connector with a token is connected without asking again.
    void restore();

    // Tests point the connector at fake servers and capture the browser opening.
    void setEndpoints(const QUrl &deviceCode, const QUrl &token, const QString &apiBase);
    void setOpener(DeviceFlowClient::Opener opener) { m_device.setOpener(std::move(opener)); }
    DeviceFlowClient *deviceClient() { return &m_device; }
    int requestTimeoutMs = 30000;

private:
    struct Api {
        bool ok = false;
        int status = 0;
        QJsonDocument doc;
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

    void listRepos(const QJsonObject &args, connectors::Done done);
    void listIssues(const QJsonObject &args, connectors::Done done);
    void readIssue(const QJsonObject &args, connectors::Done done);
    void searchIssues(const QJsonObject &args, connectors::Done done);
    void listNotifications(const QJsonObject &args, connectors::Done done);
    void createIssue(const QJsonObject &args, connectors::Done done);
    void addComment(const QJsonObject &args, connectors::Done done);

    QNetworkAccessManager m_nam;
    DeviceFlowClient m_device;
    SecretStore *m_secrets = nullptr;
    QString m_clientId;
    oauth::Tokens m_tokens;
    QString m_apiBase;
    bool m_refreshing = false;
    QList<std::function<void(bool, const QString &)>> m_refreshWaiters;
    quint64 m_epoch = 0;   // bumped by disconnect/cancel so late answers are dropped
};
