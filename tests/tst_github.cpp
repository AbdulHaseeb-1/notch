// GitHub connector against a fake GitHub: the whole device-flow sign-in (device code,
// the user code shown to the user, authorization_pending / slow_down polling, the token
// answer), then the REST tools with the exact requests checked, token refresh for
// expiring tokens, cancel and disconnect, and the pure helpers.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrlQuery>

#include "FakeHttp.h"
#include "GithubConnector.h"
#include "SecretStore.h"

namespace {

QByteArray issueJson(int number, const QString &title, const QString &repoFull, const QString &body = QString())
{
    QJsonObject o{
        { "number", number },
        { "title", title },
        { "state", "open" },
        { "user", QJsonObject{ { "login", "alice" } } },
        { "labels", QJsonArray{ QJsonObject{ { "name", "bug" } } } },
        { "comments", 2 },
        { "created_at", "2026-10-01T10:00:00Z" },
        { "html_url", "https://github.example.com/" + repoFull + "/issues/" + QString::number(number) },
    };
    if (!repoFull.isEmpty())
        o.insert("repository_url", "https://api.github.example.com/repos/" + repoFull);
    if (!body.isEmpty())
        o.insert("body", body);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

class TstGithub : public QObject
{
    Q_OBJECT
    FakeHttpServer gh;   // device-code + token endpoints and the REST API in one
    QString accessToken = "gho_test";
    int devicePolls = 0;
    int tokenRefreshes = 0;
    QUrl openedUrl;
    bool openerUsed = false;

    void routes() {
        gh.handler = [this](const HttpRequest &r) -> HttpResponse {
            const QString path = r.path();
            if (path == "/login/device/code") {
                const QUrlQuery f(QString::fromUtf8(r.body));
                if (f.queryItemValue("client_id") != "github-client")
                    return HttpResponse::json(R"({"error":"incorrect_client_credentials"})", 400);
                return HttpResponse::json(QByteArray(R"({"device_code":"dev-1","user_code":"WDJB-MJHT","verification_uri":")")
                                          + (gh.base() + "/device").toUtf8()
                                          + "\",\"expires_in\":600,\"interval\":5}");
            }
            if (path == "/login/oauth/access_token") {
                const QUrlQuery f(QString::fromUtf8(r.body));
                // No flow in this file may ever send a client secret.
                if (QString::fromUtf8(r.body).contains("client_secret"))
                    return HttpResponse::json(R"({"error":"bad"})", 400);
                if (f.queryItemValue("grant_type") == "refresh_token") {
                    ++tokenRefreshes;
                    if (f.queryItemValue("refresh_token") != "ghr_old")
                        return HttpResponse::json(R"({"error":"invalid_grant"})", 400);
                    return HttpResponse::json(R"({"access_token":"gho_new","refresh_token":"ghr_new","expires_in":28800})");
                }
                if (f.queryItemValue("device_code") != "dev-1")
                    return HttpResponse::json(R"({"error":"incorrect_device_code"})", 400);
                ++devicePolls;
                if (devicePolls == 1)
                    return HttpResponse::json(R"({"error":"authorization_pending","error_description":"pending"})", 400);
                if (devicePolls == 2)
                    return HttpResponse::json(R"({"error":"slow_down"})", 400);
                return HttpResponse::json(R"({"access_token":"gho_test","token_type":"bearer","scope":"repo,read:user"})");
            }
            // Everything below is the REST API and needs the current access token.
            if (r.header("authorization") != "Bearer " + accessToken.toUtf8())
                return HttpResponse::json(R"({"message":"Bad credentials"})", 401);
            if (path == "/user")
                return HttpResponse::json(R"({"login":"haseeb-dev","name":"Haseeb"})");
            if (path == "/user/repos")
                return HttpResponse::json(R"([{"full_name":"AbdulHaseeb-1/notch","description":"A dynamic notch","language":"C++","stargazers_count":12,"private":false,"updated_at":"2026-10-20T00:00:00Z"},
                                              {"full_name":"haseeb-dev/notes","description":"","language":"Python","stargazers_count":0,"private":true,"updated_at":"2026-09-01T00:00:00Z"}])");
            if (path == "/issues")
                return HttpResponse::json("[" + issueJson(3, "Crash on start", "AbdulHaseeb-1/notch") + "]");
            if (path == "/repos/AbdulHaseeb-1/notch/issues" && r.method == "GET")
                return HttpResponse::json("[" + issueJson(7, "Hover halo stays lit", "") + "]");
            if (path == "/repos/AbdulHaseeb-1/notch/issues/7" && r.method == "GET")
                return HttpResponse::json(issueJson(7, "Hover halo stays lit", "", "It never goes out."));
            if (path == "/search/issues")
                return HttpResponse::json(QByteArray(R"({"total_count":1,"items":[)") + issueJson(7, "Hover halo stays lit", "AbdulHaseeb-1/notch") + "]}");
            if (path == "/notifications")
                return HttpResponse::json(R"([{"unread":true,"updated_at":"2026-10-21T00:00:00Z","subject":{"title":"Hover halo stays lit","type":"Issue"},"repository":{"full_name":"AbdulHaseeb-1/notch"}}])");
            if (path == "/repos/AbdulHaseeb-1/notch/issues" && r.method == "POST")
                return HttpResponse::json("{\"number\":42,\"html_url\":\"https://github.example.com/AbdulHaseeb-1/notch/issues/42\"}", 201);
            if (path == "/repos/AbdulHaseeb-1/notch/issues/7/comments" && r.method == "POST")
                return HttpResponse::json("{\"html_url\":\"https://github.example.com/AbdulHaseeb-1/notch/issues/7#issuecomment-1\"}", 201);
            if (path == "/repos/gone/missing/issues")
                return HttpResponse::json(R"({"message":"Not Found"})", 404);
            return HttpResponse::text(404, "nope");
        };
    }

    GithubConnector *make(SecretStore *store = nullptr) {
        auto *g = new GithubConnector;
        g->setEndpoints(QUrl(gh.base() + "/login/device/code"), QUrl(gh.base() + "/login/oauth/access_token"), gh.base());
        if (store)
            g->setSecretStore(store);
        g->setClientId("github-client");
        g->deviceClient()->pollIntervalOverrideMs = 5;
        g->setOpener([this](const QUrl &url) { openedUrl = url; openerUsed = true; return true; });
        return g;
    }

    GithubConnector *connected(SecretStore *store = nullptr) {
        GithubConnector *g = make(store);
        g->connectAccount();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(g->state() == connectors::State::Connected || g->state() == connectors::State::Error, 8000); }();
        return g;
    }

    struct Out { bool ok = false; QString text; bool done = false; };
    Out run(GithubConnector *g, const QString &tool, const QJsonObject &args) {
        Out out;
        g->call(tool, args, [&out](bool ok, const QString &t) { out.ok = ok; out.text = t; out.done = true; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(out.done, 8000); }();
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(gh.start()); }
    void init() { gh.requests.clear(); routes(); devicePolls = 0; tokenRefreshes = 0; accessToken = "gho_test"; openedUrl.clear(); openerUsed = false; }

    // ---- pure helpers -------------------------------------------------------------

    void repoSlugsAreStrict() {
        QCOMPARE(github::checkRepoSlug("AbdulHaseeb-1/notch"), QStringLiteral("AbdulHaseeb-1/notch"));
        QVERIFY(github::checkRepoSlug("notch").isEmpty());
        QVERIFY(github::checkRepoSlug("a/b/c").isEmpty());
        QVERIFY(github::checkRepoSlug("a b/c").isEmpty());
        QVERIFY(github::checkRepoSlug("/x").isEmpty());
        QVERIFY(github::checkRepoSlug("../etc").isEmpty() == false);   // two parts, no space: GitHub 404s it; still one path segment each
    }

    void toolDefsSplitReadFromWrite() {
        QStringList reads, writes;
        for (const auto &d : github::toolDefs())
            (d.access == connectors::Access::Read ? reads : writes) << d.name;
        reads.sort(); writes.sort();
        QCOMPARE(reads, (QStringList{ "github_list_issues", "github_list_notifications", "github_list_repos",
                                      "github_read_issue", "github_search_issues" }));
        QCOMPARE(writes, (QStringList{ "github_add_comment", "github_create_issue" }));
        // Approval-card and gate naming come out readable.
        QCOMPARE(connectors::prettyToolName("github_create_issue"), QStringLiteral("GitHub: create issue"));
        QCOMPARE(connectors::iconForConnectorTool("github_list_repos"), QStringLiteral("code"));
    }

    // ---- the sign-in ----------------------------------------------------------------

    void cannotConnectWithoutAClientId() {
        GithubConnector g;
        if (!builtGithubIdIsEmpty())
            QSKIP("this build or environment provides a GitHub client ID");
        QVERIFY(!g.configured());
        g.connectAccount();
        QCOMPARE(g.state(), connectors::State::Error);
        QVERIFY(g.statusText().contains("client ID"));
        QVERIFY(g.tools().isEmpty());
    }

    bool builtGithubIdIsEmpty() {
        // A default-constructed connector carries only BuiltinOAuth's ID.
        GithubConnector probe;
        return probe.clientId().isEmpty();
    }

    void deviceFlowSignsInThroughPendingAndSlowDown() {
        QTemporaryDir dir;
        SecretStore store(dir.path());
        std::unique_ptr<GithubConnector> g(connected(&store));
        QCOMPARE(g->state(), connectors::State::Connected);
        QCOMPARE(g->account(), QStringLiteral("haseeb-dev"));
        QVERIFY(g->statusText().contains("@haseeb-dev"));
        QVERIFY(openerUsed);
        QCOMPARE(openedUrl, QUrl(gh.base() + "/device"));   // the browser went to the verification page
        QCOMPARE(devicePolls, 3);                          // pending, slow_down, success
        QCOMPARE(g->tools().size(), 7);
        // The device-code request asked for exactly Noch's scopes and no secret.
        const HttpRequest *dev = nullptr;
        for (const HttpRequest &r : gh.requests)
            if (r.path() == "/login/device/code")
                dev = &r;
        QVERIFY(dev);
        const QUrlQuery f(QString::fromUtf8(dev->body));
        QCOMPARE(f.queryItemValue("client_id"), QStringLiteral("github-client"));
        QCOMPARE(f.queryItemValue("scope", QUrl::FullyDecoded), QStringLiteral("repo read:user"));
        QVERIFY(!QString::fromUtf8(dev->body).contains("client_secret"));
        // Tokens are saved (encrypted where the platform supports it).
        QVERIFY(!store.get("github.tokens").isEmpty());
    }

    void theUserCodeIsShownWhileConnectingAndCancelStopsIt() {
        std::unique_ptr<GithubConnector> g(make());
        g->deviceClient()->pollIntervalOverrideMs = 60000;   // stay in "waiting for the browser"
        g->connectAccount();
        QTRY_VERIFY_WITH_TIMEOUT(g->statusText().contains("WDJB-MJHT"), 8000);
        QCOMPARE(g->state(), connectors::State::Connecting);
        g->cancelAll();
        QCOMPARE(g->state(), connectors::State::Disconnected);
        QVERIFY(g->tools().isEmpty());
    }

    void decliningInTheBrowserEndsTheSignIn() {
        gh.handler = [this](const HttpRequest &r) -> HttpResponse {
            if (r.path() == "/login/device/code")
                return HttpResponse::json(QByteArray(R"({"device_code":"dev-1","user_code":"WDJB-MJHT","verification_uri":")")
                                          + (gh.base() + "/device").toUtf8() + "\",\"expires_in\":600,\"interval\":5}");
            return HttpResponse::json(R"({"error":"access_denied"})", 400);
        };
        std::unique_ptr<GithubConnector> g(make());
        g->connectAccount();
        QTRY_COMPARE_WITH_TIMEOUT(g->state(), connectors::State::Error, 8000);
        QVERIFY(g->statusText().contains("declined"));
        QVERIFY(g->tools().isEmpty());
    }

    void tokensAreRestoredAfterARestart() {
        QTemporaryDir dir;
        {
            SecretStore store(dir.path());
            std::unique_ptr<GithubConnector> g(connected(&store));
            QCOMPARE(g->state(), connectors::State::Connected);
        }
        SecretStore again(dir.path());
        GithubConnector g2;
        g2.setSecretStore(&again);
        g2.restore();
        if (again.persistent()) {
            QCOMPARE(g2.state(), connectors::State::Connected);
            QCOMPARE(g2.account(), QStringLiteral("haseeb-dev"));
            QCOMPARE(g2.tools().size(), 7);
        } else {
            QCOMPARE(g2.state(), connectors::State::Disconnected);
        }
    }

    void anExpiredStoredTokenIsRefreshedBeforeTheCall() {
        accessToken = "gho_new";   // the API only accepts the refreshed token
        SecretStore store;
        store.set("github.tokens", QString::fromUtf8(QJsonDocument(QJsonObject{
            { "tokens", QJsonObject{ { "access", "gho_old" }, { "refresh", "ghr_old" }, { "expiresAtMs", 1.0 } } },
            { "account", "haseeb-dev" } }).toJson(QJsonDocument::Compact)));
        GithubConnector g;
        g.setEndpoints(QUrl(gh.base() + "/login/device/code"), QUrl(gh.base() + "/login/oauth/access_token"), gh.base());
        g.setSecretStore(&store);
        g.setClientId("github-client");
        g.restore();
        QCOMPARE(g.state(), connectors::State::Connected);
        const Out o = run(&g, "github_list_repos", {});
        QVERIFY2(o.ok, qPrintable(o.text));
        QCOMPARE(tokenRefreshes, 1);
        QVERIFY(o.text.contains("AbdulHaseeb-1/notch"));
        // The new token was kept: no second refresh for the next call.
        QVERIFY(run(&g, "github_list_repos", {}).ok);
        QCOMPARE(tokenRefreshes, 1);
    }

    // ---- the tools --------------------------------------------------------------------

    void listReposRendersWhatGitHubSent() {
        std::unique_ptr<GithubConnector> g(connected());
        const Out o = run(g.get(), "github_list_repos", { { "max_results", 5 } });
        QVERIFY2(o.ok, qPrintable(o.text));
        QVERIFY(o.text.contains("AbdulHaseeb-1/notch"));
        QVERIFY(o.text.contains("C++"));
        QVERIFY(o.text.contains("private"));
        QCOMPARE(gh.count("GET", "/user/repos"), 1);
        for (const HttpRequest &r : gh.requests)
            if (r.path() == "/user/repos")
                QCOMPARE(r.query().queryItemValue("per_page"), QStringLiteral("5"));
    }

    void listAndReadIssues() {
        std::unique_ptr<GithubConnector> g(connected());
        const Out mine = run(g.get(), "github_list_issues", {});
        QVERIFY2(mine.ok, qPrintable(mine.text));
        QVERIFY(mine.text.contains("AbdulHaseeb-1/notch #3"));   // repo taken from repository_url
        QVERIFY(mine.text.contains("Crash on start"));
        const Out inRepo = run(g.get(), "github_list_issues", { { "repo", "AbdulHaseeb-1/notch" } });
        QVERIFY(inRepo.ok);
        QVERIFY(inRepo.text.contains("#7"));
        const Out one = run(g.get(), "github_read_issue", { { "repo", "AbdulHaseeb-1/notch" }, { "number", 7 } });
        QVERIFY2(one.ok, qPrintable(one.text));
        QVERIFY(one.text.contains("Hover halo stays lit"));
        QVERIFY(one.text.contains("It never goes out."));
        QVERIFY(one.text.contains("labels: bug"));
    }

    void searchAndNotifications() {
        std::unique_ptr<GithubConnector> g(connected());
        const Out s = run(g.get(), "github_search_issues", { { "query", "halo repo:AbdulHaseeb-1/notch" } });
        QVERIFY2(s.ok, qPrintable(s.text));
        QVERIFY(s.text.contains("1 match"));
        QVERIFY(s.text.contains("Hover halo stays lit"));
        const Out n = run(g.get(), "github_list_notifications", {});
        QVERIFY2(n.ok, qPrintable(n.text));
        QVERIFY(n.text.contains("AbdulHaseeb-1/notch"));
        QVERIFY(n.text.contains("(Issue)"));
    }

    void createIssueAndCommentPostTheExactPayload() {
        std::unique_ptr<GithubConnector> g(connected());
        const Out c = run(g.get(), "github_create_issue",
                          { { "repo", "AbdulHaseeb-1/notch" }, { "title", "New bug" }, { "body", "Steps…" } });
        QVERIFY2(c.ok, qPrintable(c.text));
        QVERIFY(c.text.contains("#42"));
        const Out cm = run(g.get(), "github_add_comment",
                           { { "repo", "AbdulHaseeb-1/notch" }, { "number", 7 }, { "body", "Looking into it." } });
        QVERIFY2(cm.ok, qPrintable(cm.text));
        QVERIFY(cm.text.contains("Comment added"));
        const HttpRequest *post = nullptr;
        for (const HttpRequest &r : gh.requests)
            if (r.method == "POST" && r.path() == "/repos/AbdulHaseeb-1/notch/issues")
                post = &r;
        QVERIFY(post);
        const QJsonObject body = QJsonDocument::fromJson(post->body).object();
        QCOMPARE(body.value("title").toString(), QStringLiteral("New bug"));
        QCOMPARE(body.value("body").toString(), QStringLiteral("Steps…"));
    }

    void badArgumentsAreRejectedBeforeAnyRequest() {
        std::unique_ptr<GithubConnector> g(connected());
        gh.requests.clear();
        QVERIFY(!run(g.get(), "github_read_issue", { { "repo", "not-a-slug" }, { "number", 1 } }).ok);
        QVERIFY(!run(g.get(), "github_read_issue", { { "repo", "a/b" }, { "number", 0 } }).ok);
        QVERIFY(!run(g.get(), "github_create_issue", { { "repo", "a/b" }, { "body", "x" } }).ok);   // no title
        QVERIFY(!run(g.get(), "github_add_comment", { { "repo", "a/b" }, { "number", 1 } }).ok);     // no body
        QVERIFY(!run(g.get(), "github_delete_repo", {}).ok);                                          // no such tool
        QVERIFY(gh.requests.isEmpty());
    }

    void aMissingRepoIsAReadableError() {
        std::unique_ptr<GithubConnector> g(connected());
        const Out o = run(g.get(), "github_list_issues", { { "repo", "gone/missing" } });
        QVERIFY(!o.ok);
        QVERIFY(o.text.contains("does not have that repository"));
    }

    void disconnectForgetsTheTokens() {
        QTemporaryDir dir;
        SecretStore store(dir.path());
        std::unique_ptr<GithubConnector> g(connected(&store));
        QVERIFY(!store.get("github.tokens").isEmpty());
        g->disconnectAccount();
        QCOMPARE(g->state(), connectors::State::Disconnected);
        QVERIFY(g->tools().isEmpty());
        QVERIFY(store.get("github.tokens").isEmpty());
        const Out o = run(g.get(), "github_list_repos", {});
        QVERIFY(!o.ok);
        QVERIFY(o.text.contains("not connected"));
    }
};

QTEST_MAIN(TstGithub)
#include "tst_github.moc"
