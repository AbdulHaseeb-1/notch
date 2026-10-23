// ConnectorRegistry: what the agent and the Settings page talk to. Saved settings and
// secrets (encrypted, never in connectors.json), restart behaviour, "always allow"
// grants, and the lists the model and the page read.
#include <QtTest>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "ConnectorRegistry.h"
#include "FakeHttp.h"
#include "FakeMcp.h"
#include "GmailConnector.h"
#include "McpConnector.h"

class TstConnectorRegistry : public QObject
{
    Q_OBJECT
    FakeHttpServer mcpServer;

    QJsonObject savedConfig(const QString &dir) {
        QFile f(QDir(dir).filePath("connectors.json"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    QVariantMap app(ConnectorRegistry &r, const QString &id) {
        for (const QVariant &v : r.connectorList())
            if (v.toMap().value("id").toString() == id)
                return v.toMap();
        return {};
    }
    bool waitFor(ConnectorRegistry &r, const QString &id, const QString &state) {
        for (int i = 0; i < 200; ++i) {
            if (app(r, id).value("state").toString() == state)
                return true;
            QTest::qWait(50);
        }
        return false;
    }

private slots:
    void initTestCase() {
        QVERIFY(mcpServer.start());
        mcpServer.handler = [](const HttpRequest &r) -> HttpResponse {
            if (r.header("authorization") != "Bearer tok-123")
                return HttpResponse::text(401, "no");
            const QJsonObject req = QJsonDocument::fromJson(r.body).object();
            if (!req.contains("id"))
                return HttpResponse::text(202, "");
            return HttpResponse::json(QJsonDocument(fakemcp::answer(req)).toJson(QJsonDocument::Compact));
        };
    }

    void startsWithGmailDisconnectedAndNoTools() {
        ConnectorRegistry r;
        QVERIFY(r.find("gmail"));
        QCOMPARE(app(r, "gmail").value("state").toString(), QStringLiteral("disconnected"));
        QVERIFY(!r.hasTools());
        QVERIFY(r.tools().isEmpty());
        QVERIFY(!r.hasTool("gmail_send_message"));   // not connected: not a tool
        QCOMPARE(r.connectorList().size(), 1);
        QVERIFY(!app(r, "gmail").value("permissions").toList().isEmpty());
        QVERIFY(app(r, "gmail").value("permissions").toList().last().toMap().value("write").toBool());
    }

    void addingServersValidatesAndGivesUniqueIds() {
        ConnectorRegistry r;
        QVERIFY(!r.addMcpServer("", "http", "https://example.com/mcp", "").isEmpty());
        QVERIFY(!r.addMcpServer("X", "http", "http://example.com/mcp", "").isEmpty());
        QVERIFY(!r.addMcpServer("X", "stdio", "", "").isEmpty());
        QCOMPARE(r.connectorList().size(), 1);   // nothing was added
        const QString url = mcpServer.base() + "/mcp";
        QVERIFY(r.addMcpServer("GitHub", "http", url, "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(r.addMcpServer("GitHub", "http", url, "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(r.find("mcp_github"));
        QVERIFY(r.find("mcp_github_2"));
        QVERIFY(waitFor(r, "mcp_github", "connected"));
    }

    void connectedServerToolsReachTheAgentWithTheirKinds() {
        ConnectorRegistry r;
        QVERIFY(r.addMcpServer("GitHub", "http", mcpServer.base() + "/mcp", "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(waitFor(r, "mcp_github", "connected"));
        ConnectorRegistry::Tool t;
        QVERIFY(r.findTool("mcp_github_list_issues", &t));
        QCOMPARE(t.def.access, connectors::Access::Read);
        QCOMPARE(t.connectorId, QStringLiteral("mcp_github"));
        QVERIFY(r.findTool("mcp_github_create_issue_", &t));
        QCOMPARE(t.def.access, connectors::Access::Write);
        // The schema used to validate calls: required argument and types.
        const ToolSchema *schema = r.schemaFor("mcp_github_list_issues");
        QVERIFY(schema);
        QCOMPARE(schema->requiredArgs(), QStringList{ "repo" });
        QVERIFY(!r.schemaFor("mcp_github_nope"));
        // Running one.
        QString text;
        bool done = false;
        r.callTool("mcp_github_list_issues", { { "repo", "noch" } }, [&](bool ok, const QString &t2) { QVERIFY(ok); text = t2; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(text, QStringLiteral("issue #1 in noch"));
        // An unknown tool answers with an error instead of hanging.
        done = false;
        bool ok = true;
        r.callTool("gmail_send_message", {}, [&](bool o, const QString &) { ok = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!ok);
        // The page's list shows each tool and whether it asks.
        const QVariantList tools = app(r, "mcp_github").value("tools").toList();
        QVERIFY(tools.size() >= 3);
        bool sawWriteAsking = false;
        for (const QVariant &v : tools)
            if (v.toMap().value("name").toString() == "mcp_github_create_issue_")
                sawWriteAsking = v.toMap().value("write").toBool() && !v.toMap().value("always").toBool();
        QVERIFY(sawWriteAsking);
    }

    void settingsAndSecretsSurviveARestartSeparately() {
        QTemporaryDir dir;
        const QString url = mcpServer.base() + "/mcp";
        {
            ConnectorRegistry r;
            r.setStorageDir(dir.path());
            r.load();
            r.setGoogleClientId("123-abc.apps.googleusercontent.com");
            r.setGoogleClientSecret("GOCSPX-very-secret");
            QVERIFY(r.addMcpServer("GitHub", "http", url, "Authorization: Bearer tok-123").isEmpty());
            QVERIFY(waitFor(r, "mcp_github", "connected"));
            r.setAlwaysAllow("mcp_github_create_issue_", true);
        }
        // The plain settings file holds no secret ...
        QFile f(QDir(dir.path()).filePath("connectors.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray plain = f.readAll();
        QVERIFY(plain.contains("123-abc.apps.googleusercontent.com"));
        QVERIFY(plain.contains(url.toUtf8()));
        QVERIFY(!plain.contains("tok-123"));
        QVERIFY(!plain.contains("GOCSPX"));
        // ... and neither does anything else on disk.
        for (QDirIterator it(dir.path(), QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            QFile file(it.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray raw = file.readAll();
            if (secrets::encryptionAvailable() || !it.filePath().endsWith(".json"))
                QVERIFY2(!raw.contains("tok-123") && !raw.contains("GOCSPX"), qPrintable(it.filePath()));
        }

        // Restart: the server comes back connected, the grant and the client ID are there.
        ConnectorRegistry again;
        again.setStorageDir(dir.path());
        again.load();
        QVERIFY(again.find("mcp_github"));
        QCOMPARE(app(again, "gmail").value("clientId").toString(), QStringLiteral("123-abc.apps.googleusercontent.com"));
        QVERIFY(again.alwaysAllowed("mcp_github_create_issue_"));
        if (secrets::encryptionAvailable()) {
            QVERIFY(waitFor(again, "mcp_github", "connected"));   // the encrypted token was read back
            QVERIFY(again.hasTool("mcp_github_list_issues"));
            QVERIFY(app(again, "gmail").value("hasClientSecret").toBool());
        }
    }

    void disconnectingAServerKeepsItOffAfterARestart() {
        QTemporaryDir dir;
        {
            ConnectorRegistry r;
            r.setStorageDir(dir.path());
            QVERIFY(r.addMcpServer("GitHub", "http", mcpServer.base() + "/mcp", "Authorization: Bearer tok-123").isEmpty());
            QVERIFY(waitFor(r, "mcp_github", "connected"));
            r.disconnectApp("mcp_github");
            QCOMPARE(app(r, "mcp_github").value("state").toString(), QStringLiteral("disconnected"));
            QVERIFY(!r.hasTools());
        }
        QVERIFY(!savedConfig(dir.path()).value("mcp").toArray().first().toObject().value("autoConnect").toBool());
        ConnectorRegistry again;
        again.setStorageDir(dir.path());
        again.load();
        QTest::qWait(300);
        QCOMPARE(app(again, "mcp_github").value("state").toString(), QStringLiteral("disconnected"));
        // Connect brings it back (and turns auto-connect on again).
        again.connectApp("mcp_github");
        QVERIFY(waitFor(again, "mcp_github", "connected"));
    }

    void removingAServerForgetsItsSecretsAndGrants() {
        QTemporaryDir dir;
        ConnectorRegistry r;
        r.setStorageDir(dir.path());
        QVERIFY(r.addMcpServer("GitHub", "http", mcpServer.base() + "/mcp", "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(waitFor(r, "mcp_github", "connected"));
        r.setAlwaysAllow("mcp_github_create_issue_", true);
        r.setAlwaysAllow("gmail_send_message", true);
        QVERIFY(!r.secrets()->get("mcp.mcp_github.secrets").isEmpty());
        r.removeConnector("mcp_github");
        QVERIFY(!r.find("mcp_github"));
        QVERIFY(r.secrets()->get("mcp.mcp_github.secrets").isEmpty());
        QVERIFY(!r.alwaysAllowed("mcp_github_create_issue_"));
        QVERIFY(r.alwaysAllowed("gmail_send_message"));   // another app's grant stays
        QVERIFY(savedConfig(dir.path()).value("mcp").toArray().isEmpty());
        // Built-in apps are disconnected, not removed.
        r.removeConnector("gmail");
        QVERIFY(r.find("gmail"));
    }

    void updatingAServerKeepsItsIdAndSavedSecrets() {
        ConnectorRegistry r;
        QVERIFY(r.addMcpServer("GitHub", "http", mcpServer.base() + "/mcp", "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(waitFor(r, "mcp_github", "connected"));
        // Rename and re-point without retyping the token.
        QVERIFY(r.updateMcpServer("mcp_github", "GH", "http", mcpServer.base() + "/mcp", "").isEmpty());
        QVERIFY(r.find("mcp_github"));
        QCOMPARE(app(r, "mcp_github").value("name").toString(), QStringLiteral("GH"));
        QVERIFY(waitFor(r, "mcp_github", "connected"));
        QVERIFY(!r.updateMcpServer("mcp_nope", "x", "http", "https://e.com", "").isEmpty());
    }

    void grantsAreRevocableOneByOneOrAll() {
        ConnectorRegistry r;
        r.setAlwaysAllow("gmail_send_message", true);
        r.setAlwaysAllow("gmail_create_draft", true);
        QCOMPARE(r.alwaysAllowedTools().size(), 2);
        r.revokeAlwaysAllow("gmail_send_message");
        QVERIFY(!r.alwaysAllowed("gmail_send_message"));
        QVERIFY(r.alwaysAllowed("gmail_create_draft"));
        r.revokeAllAlwaysAllow();
        QVERIFY(r.alwaysAllowedTools().isEmpty());
    }

    void changesAreAnnouncedToTheAgent() {
        ConnectorRegistry r;
        QSignalSpy tools(&r, &ConnectorRegistry::toolsChanged);
        QSignalSpy changed(&r, &ConnectorRegistry::changed);
        QVERIFY(r.addMcpServer("GitHub", "http", mcpServer.base() + "/mcp", "Authorization: Bearer tok-123").isEmpty());
        QVERIFY(waitFor(r, "mcp_github", "connected"));
        QVERIFY(tools.size() >= 2);   // added, then connected
        QVERIFY(changed.size() >= 2);
        const int before = int(tools.size());
        r.setAlwaysAllow("mcp_github_create_issue_", true);
        QVERIFY(tools.size() > before);   // the gate must learn about the grant
    }

    void presetsAreUsableAsTheyAre() {
        ConnectorRegistry r;
        for (const QVariant &v : r.presets()) {
            const QVariantMap p = v.toMap();
            QVERIFY(!p.value("name").toString().isEmpty());
            QVERIFY(mcp::validateSpec(p.value("transport").toString(), p.value("target").toString()).isEmpty());
        }
    }
};

QTEST_MAIN(TstConnectorRegistry)
#include "tst_connectorregistry.moc"
