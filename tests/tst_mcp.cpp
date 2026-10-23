// MCP connector: the pure parsing helpers, then a full session over Streamable HTTP
// (plain JSON and event-stream replies, session id, headers from the encrypted secrets)
// and over stdio (this test binary started again as a tiny MCP server).
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

#include <iostream>
#include <string>

#include "FakeHttp.h"
#include "FakeMcp.h"
#include "McpConnector.h"
#include "SecretStore.h"

namespace {

// The stdio fake: reads one JSON message per line from stdin, answers on stdout.
int runFakeStdioServer()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject req = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        if (!req.contains("id"))
            continue;   // a notification
        if (req.value("method").toString() == "tools/call"
                && req.value("params").toObject().value("name").toString() == "no_hint") {
            // Noise on stdout that is not JSON-RPC must be ignored by the client.
            std::cout << "debug: starting something\n";
        }
        std::cout << QJsonDocument(fakemcp::answer(req)).toJson(QJsonDocument::Compact).toStdString() << "\n" << std::flush;
    }
    return 0;
}

} // namespace

class TstMcp : public QObject
{
    Q_OBJECT
    FakeHttpServer srv;
    QString seenSession;

    struct Out { bool ok = false; QString text; bool done = false; };
    Out run(McpConnector &c, const QString &tool, const QJsonObject &args) {
        Out out;
        c.call(tool, args, [&out](bool ok, const QString &t) { out.ok = ok; out.text = t; out.done = true; });
        [&] { QTRY_VERIFY_WITH_TIMEOUT(out.done, 10000); }();
        return out;
    }
    void waitConnected(McpConnector &c) {
        QTRY_VERIFY_WITH_TIMEOUT(c.state() == connectors::State::Connected || c.state() == connectors::State::Error, 15000);
    }

private slots:
    void initTestCase() { QVERIFY(srv.start()); }
    void init() {
        srv.requests.clear();
        seenSession.clear();
        srv.handler = [this](const HttpRequest &r) -> HttpResponse {
            const QJsonObject req = QJsonDocument::fromJson(r.body).object();
            if (r.header("authorization") != "Bearer tok-123")
                return HttpResponse::text(401, "no");
            if (!req.contains("id"))
                return HttpResponse::text(202, "");
            const QByteArray body = QJsonDocument(fakemcp::answer(req)).toJson(QJsonDocument::Compact);
            HttpResponse resp;
            if (req.value("method").toString() == "initialize")
                resp.headers = { { "Mcp-Session-Id", "sess-1" } };
            else
                seenSession = QString::fromLatin1(r.header("mcp-session-id"));
            // tools/list over an event stream, everything else as plain JSON.
            if (req.value("method").toString() == "tools/list") {
                resp.contentType = "text/event-stream";
                resp.body = "event: message\ndata: " + body + "\n\n";
            } else {
                resp.body = body;
            }
            return resp;
        };
    }

    // ---- helpers ------------------------------------------------------------------

    void headerAndEnvLines() {
        const auto h = mcp::parseHeaderLines("# comment\nAuthorization: Bearer abc\n\nX-Org:  acme \nbad line\nBad Name: x");
        QCOMPARE(h.size(), 2);
        QCOMPARE(h.at(0).first, QStringLiteral("Authorization"));
        QCOMPARE(h.at(0).second, QStringLiteral("Bearer abc"));
        QCOMPARE(h.at(1).second, QStringLiteral("acme"));
        const auto e = mcp::parseEnvLines("GITHUB_TOKEN=ghp_x=y\n1BAD=no\nGOOD_1 = v");
        QCOMPARE(e.size(), 2);
        QCOMPARE(e.at(0).second, QStringLiteral("ghp_x=y"));
        QCOMPARE(e.at(1).first, QStringLiteral("GOOD_1"));
    }

    void specValidation() {
        QVERIFY(mcp::validateSpec("http", "https://example.com/mcp").isEmpty());
        QVERIFY(mcp::validateSpec("http", "http://127.0.0.1:8080/mcp").isEmpty());
        QVERIFY(!mcp::validateSpec("http", "http://example.com/mcp").isEmpty());   // plain http across a network
        QVERIFY(!mcp::validateSpec("http", "file:///etc/passwd").isEmpty());
        QVERIFY(!mcp::validateSpec("http", "").isEmpty());
        QVERIFY(mcp::validateSpec("stdio", "npx -y some-server").isEmpty());
        QVERIFY(!mcp::validateSpec("stdio", "   ").isEmpty());
        QVERIFY(!mcp::validateSpec("carrier-pigeon", "x").isEmpty());
        QCOMPARE(mcp::splitCommand("\"C:/Program Files/node/node.exe\" server.js --flag"),
                 (QStringList{ "C:/Program Files/node/node.exe", "server.js", "--flag" }));
    }

    void toolNamesAreNamespacedSafeAndUnique() {
        QCOMPARE(mcp::exposedName("github", "list_issues", {}), QStringLiteral("mcp_github_list_issues"));
        QCOMPARE(mcp::exposedName("github", "create issue!", {}), QStringLiteral("mcp_github_create_issue_"));
        QCOMPARE(mcp::exposedName("github", "a", { "mcp_github_a" }), QStringLiteral("mcp_github_a_2"));
        const QString longName = mcp::exposedName("github", QString(200, 'x'), {});
        QCOMPARE(longName.size(), 64);
        QVERIFY(longName.startsWith("mcp_"));   // a server can never be named bash or web_fetch
    }

    void resultTextHandlesEveryContentType() {
        bool err = false;
        QJsonObject r{ { "content", QJsonArray{ QJsonObject{ { "type", "text" }, { "text", "hello" } },
                                                 QJsonObject{ { "type", "image" }, { "data", "AAAA" } },
                                                 QJsonObject{ { "type", "resource" }, { "resource", QJsonObject{ { "uri", "file:///a" }, { "text", "res text" } } } } } },
                       { "isError", true } };
        const QString t = mcp::resultText(r, &err);
        QVERIFY(err);
        QVERIFY(t.contains("hello") && t.contains("res text") && t.contains("image content not shown"));
        QVERIFY(!t.contains("AAAA"));
        QCOMPARE(mcp::resultText(QJsonObject{ { "structuredContent", QJsonObject{ { "n", 1 } } } }, &err), QStringLiteral("{\"n\":1}"));
        QCOMPARE(mcp::sseData("event: message\ndata: {\"a\":1}\ndata: more\n"), QStringLiteral("{\"a\":1}\nmore"));
    }

    // ---- Streamable HTTP ----------------------------------------------------------------

    void httpSessionListsToolsAndRunsThem() {
        SecretStore secrets;
        secrets.set("mcp.mcp_gh.secrets", "Authorization: Bearer tok-123\n");
        mcp::Spec spec{ "mcp_gh", "GitHub", "http", srv.base() + "/mcp", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        waitConnected(c);
        QCOMPARE(c.state(), connectors::State::Connected);
        QVERIFY(c.statusText().contains("fake-server"));

        QMap<QString, connectors::ToolDef> byName;
        for (const auto &d : c.tools())
            byName.insert(d.name, d);
        QCOMPARE(byName.size(), 4);   // three on page one, one on page two
        QVERIFY(byName.contains("mcp_gh_list_issues"));
        QVERIFY(byName.contains("mcp_gh_create_issue_"));
        QVERIFY(byName.contains("mcp_gh_second_page"));
        // Read-only only when the server says so.
        QCOMPARE(byName["mcp_gh_list_issues"].access, connectors::Access::Read);
        QCOMPARE(byName["mcp_gh_create_issue_"].access, connectors::Access::Write);
        QCOMPARE(byName["mcp_gh_no_hint"].access, connectors::Access::Write);
        // The server's description is cleaned and tagged with where it came from.
        QVERIFY(!byName["mcp_gh_list_issues"].description.contains(QChar(0x200B)));
        QVERIFY(byName["mcp_gh_list_issues"].description.startsWith("[GitHub]"));
        // And validated like any other tool.
        const ToolSchema schema = connectors::toToolSchema(byName["mcp_gh_list_issues"]);
        QCOMPARE(schema.requiredArgs(), QStringList{ "repo" });
        QVERIFY(schema.findArg("limit") && schema.findArg("limit")->type == "integer");

        // The secret header went out, and the session id came back on later requests.
        QVERIFY(!srv.requests.isEmpty());
        QCOMPARE(srv.requests.first().header("authorization"), QByteArray("Bearer tok-123"));
        QVERIFY(srv.requests.first().header("accept").contains("text/event-stream"));
        QCOMPARE(seenSession, QStringLiteral("sess-1"));

        const Out ok = run(c, "mcp_gh_list_issues", { { "repo", "noch" } });
        QVERIFY2(ok.ok, qPrintable(ok.text));
        QCOMPARE(ok.text, QStringLiteral("issue #1 in noch"));
        const Out bad = run(c, "mcp_gh_create_issue_", { { "title", "x" } });
        QVERIFY(!bad.ok);
        QCOMPARE(bad.text, QStringLiteral("Quota exceeded"));
        const Out img = run(c, "mcp_gh_no_hint", {});
        QVERIFY(img.ok);
        QVERIFY(img.text.contains("not shown"));
        // The call used the server's own tool name.
        bool sawOriginal = false;
        for (const HttpRequest &r : srv.requests)
            if (QJsonDocument::fromJson(r.body).object().value("params").toObject().value("name").toString() == "create issue!")
                sawOriginal = true;
        QVERIFY(sawOriginal);
        QVERIFY(!run(c, "mcp_gh_unknown", {}).ok);

        c.disconnectAccount();
        QCOMPARE(c.state(), connectors::State::Disconnected);
        QVERIFY(c.tools().isEmpty());
        QVERIFY(!c.spec().autoConnect);
    }

    void httpRefusedCredentialsAreSaidPlainly() {
        SecretStore secrets;   // no token
        mcp::Spec spec{ "mcp_gh", "GitHub", "http", srv.base() + "/mcp", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        waitConnected(c);
        QCOMPARE(c.state(), connectors::State::Error);
        QVERIFY2(c.statusText().contains("refused the credentials"), qPrintable(c.statusText()));
    }

    void secretsAreNotSentToAPlainHttpNonLocalServer() {
        // The spec check refuses such a server outright.
        SecretStore secrets;
        mcp::Spec spec{ "mcp_x", "X", "http", "http://example.com/mcp", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        QCOMPARE(c.state(), connectors::State::Error);
        QVERIFY(c.statusText().contains("https"));
    }

    void httpServerThatNeverAnswersTimesOut() {
        srv.handler = [](const HttpRequest &) { return HttpResponse::text(500, "boom"); };
        SecretStore secrets;
        mcp::Spec spec{ "mcp_x", "X", "http", srv.base() + "/mcp", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        waitConnected(c);
        QCOMPARE(c.state(), connectors::State::Error);
        QVERIFY(c.statusText().contains("500"));
    }

    // ---- stdio -----------------------------------------------------------------------------

    void stdioServerRoundTrip() {
        SecretStore secrets;
        const QString exe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
        mcp::Spec spec{ "mcp_local", "Local", "stdio", "\"" + exe + "\" --fake-mcp-server", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        waitConnected(c);
        QVERIFY2(c.state() == connectors::State::Connected, qPrintable(c.statusText()));
        QCOMPARE(c.tools().size(), 4);
        const Out ok = run(c, "mcp_local_list_issues", { { "repo", "r" } });
        QVERIFY2(ok.ok, qPrintable(ok.text));
        QCOMPARE(ok.text, QStringLiteral("issue #1 in r"));
        // A stray log line on stdout does not derail the next call.
        QVERIFY(run(c, "mcp_local_no_hint", {}).ok);
        QVERIFY(run(c, "mcp_local_list_issues", { { "repo", "again" } }).ok);
        c.disconnectAccount();
        QCOMPARE(c.state(), connectors::State::Disconnected);
    }

    void stdioServerThatDiesIsReported() {
        SecretStore secrets;
        // Exits at once with an error.
        const QString exe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
        mcp::Spec spec{ "mcp_dead", "Dead", "stdio", "\"" + exe + "\" --no-such-mode-exit", true };
        McpConnector c(spec, &secrets);
        c.initTimeoutMs = 4000;
        c.connectAccount();
        waitConnected(c);
        QCOMPARE(c.state(), connectors::State::Error);
    }

    void aMissingProgramIsReported() {
        SecretStore secrets;
        mcp::Spec spec{ "mcp_none", "None", "stdio", "definitely-not-a-real-program-xyz --flag", true };
        McpConnector c(spec, &secrets);
        c.connectAccount();
        waitConnected(c);
        QCOMPARE(c.state(), connectors::State::Error);
        QVERIFY2(c.statusText().contains("Could not start"), qPrintable(c.statusText()));
    }
};

int main(int argc, char **argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "--fake-mcp-server")
        return runFakeStdioServer();
    if (argc > 1 && QByteArray(argv[1]) == "--no-such-mode-exit")
        return 3;
    QCoreApplication app(argc, argv);
    TstMcp t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mcp.moc"
