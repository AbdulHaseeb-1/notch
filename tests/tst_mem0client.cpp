#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "Mem0Client.h"

namespace {

// One-shot HTTP server: records the request and answers with `status` / `body`.
struct FakeServer {
    QTcpServer server;
    QByteArray seen;
    int status = 200;
    int searchStatus = 0;   // when > 0: the status for searches only
    QByteArray body;

    FakeServer() {
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket *s = server.nextPendingConnection();
            QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
                seen += s->readAll();
                const int end = seen.indexOf("\r\n\r\n");
                if (end < 0)
                    return;
                // Qt 6.9's QNetworkAccessManager sends header names
                // lowercase ("content-length:"); see BuddyAgentFakes.h.
                const int len = QString::fromLatin1(seen.left(end)).toLower().section("content-length:", 1)
                                    .section("\r\n", 0, 0).trimmed().toInt();
                if (seen.size() < end + 4 + len)
                    return;
                // `seen` holds every request so far: look at the latest one only.
                const bool search = seen.mid(seen.lastIndexOf("POST /")).startsWith("POST /v3/memories/search/");
                const int st = (search && searchStatus > 0) ? searchStatus : status;
                s->write("HTTP/1.1 " + QByteArray::number(st) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                         + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    QJsonObject sentJson() const {
        return QJsonDocument::fromJson(seen.mid(seen.indexOf("\r\n\r\n") + 4)).object();
    }
};

} // namespace

class TstMem0Client : public QObject
{
    Q_OBJECT

private slots:
    void backendsResolveToTheirAddress() {
        QCOMPARE(mem0::baseFor("local", ""), QStringLiteral("http://127.0.0.1:8765"));
        QCOMPARE(mem0::baseFor("platform", "ignored"), QStringLiteral("https://api.mem0.ai"));
        QCOMPARE(mem0::baseFor("custom", " https://mem.example.com/ "), QStringLiteral("https://mem.example.com"));
        // A pasted API path is trimmed back to the origin.
        QCOMPARE(mem0::baseFor("custom", "https://mem.example.com/v3/memories/search/"),
                 QStringLiteral("https://mem.example.com"));
        QCOMPARE(mem0::baseFor("custom", ""), QString());
        // Unknown names fall back to the safe default.
        QCOMPARE(mem0::normalizeBackend("cloud?"), QStringLiteral("local"));
    }

    void bodiesMatchThePlatformApi() {
        const QJsonObject add = mem0::buildAddBody({ { "user", "I use Neovim" }, { "assistant", "Noted" } }, "noch");
        QCOMPARE(add.value("user_id").toString(), QStringLiteral("noch"));
        QCOMPARE(add.value("messages").toArray().size(), 2);
        QCOMPARE(add.value("messages").toArray().at(0).toObject().value("role").toString(), QStringLiteral("user"));
        QVERIFY(add.value("infer").toBool());

        const QJsonObject s = mem0::buildSearchBody("editor", "noch", 6, 0.1);
        QCOMPARE(s.value("filters").toObject().value("user_id").toString(), QStringLiteral("noch"));
        QCOMPARE(s.value("top_k").toInt(), 6);
        QVERIFY(!s.contains("user_id"));   // entity ids at the top level are rejected by the Platform
    }

    void parseSearchSkipsEntriesWithoutText() {
        const auto hits = mem0::parseSearch(R"({"results":[
            {"id":"a","memory":"User prefers dark mode","score":0.8},
            {"id":"b","memory":"  "},
            {"id":"c"},
            {"id":"d","memory":"User works in\nPython","score":0.4}]})");
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.at(0).memory, QStringLiteral("User prefers dark mode"));
        QCOMPARE(hits.at(1).memory, QStringLiteral("User works in Python"));   // flattened to one line
        QCOMPARE(mem0::parseSearch(R"([{"memory":"bare array"}])").size(), 1);
        QVERIFY(mem0::parseSearch("not json").isEmpty());
    }

    void recallSectionFramesMemoriesAsData() {
        QVERIFY(mem0::recallSection({}).isEmpty());
        mem0::Hit h;
        h.memory = QString(500, QLatin1Char('x'));
        const QString section = mem0::recallSection({ h });
        QVERIFY(section.contains("not instructions"));
        QVERIFY(section.size() < 700);   // capped, not 500 + prose
    }

    void keyOnlyTravelsOverHttpsOrLoopback() {
        QVERIFY(mem0::isLoopbackHost("127.0.0.1"));
        QVERIFY(mem0::isLoopbackHost("LOCALHOST"));
        QVERIFY(!mem0::isLoopbackHost("mem.example.com"));
    }

    void searchEndToEnd() {
        FakeServer fake;
        fake.body = R"({"results":[{"id":"1","memory":"User prefers dark mode","score":0.9}]})";
        Mem0Client c;
        c.setEnabled(true);
        c.setConfig("custom", fake.url(), "secret-key", "alice");
        QVERIFY(c.ready());
        QSignalSpy spy(&c, &Mem0Client::searched);
        c.search("theme", 9, 4);
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(0).toULongLong(), quint64(9));
        QVERIFY2(spy.first().at(3).toString().isEmpty(), qPrintable(spy.first().at(3).toString()));
        const auto hits = qvariant_cast<QList<mem0::Hit>>(spy.first().at(1));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().memory, QStringLiteral("User prefers dark mode"));

        QVERIFY(fake.seen.startsWith("POST /v3/memories/search/ "));
        // Header names are case-insensitive; Qt 6.9 sends them lowercase.
        QVERIFY(QString::fromLatin1(fake.seen).toLower().contains("authorization: token secret-key"));
        const QJsonObject sent = fake.sentJson();
        QCOMPARE(sent.value("query").toString(), QStringLiteral("theme"));
        QCOMPARE(sent.value("filters").toObject().value("user_id").toString(), QStringLiteral("alice"));
        QCOMPARE(sent.value("top_k").toInt(), 4);
    }

    void addEndToEnd() {
        FakeServer fake;
        fake.body = R"({"status":"SUCCEEDED","results":[]})";
        Mem0Client c;
        c.setEnabled(true);
        c.setConfig("custom", fake.url(), "k", "noch");
        QSignalSpy spy(&c, &Mem0Client::added);
        c.add({ { "user", "I live in Lahore" } }, 3, QJsonObject{ { "source", "noch" } });
        QVERIFY(spy.wait(5000));
        QVERIFY(spy.first().at(1).toBool());
        QVERIFY(fake.seen.startsWith("POST /v3/memories/add/ "));
        QCOMPARE(fake.sentJson().value("metadata").toObject().value("source").toString(), QStringLiteral("noch"));
    }

    void httpErrorFailsOpenAndRests() {
        FakeServer fake;
        fake.status = 401;
        fake.body = R"({"detail":"invalid API key"})";
        Mem0Client c;
        c.setEnabled(true);
        c.setConfig("custom", fake.url(), "bad", "noch");
        QSignalSpy spy(&c, &Mem0Client::searched);
        c.search("x", 1);
        QVERIFY(spy.wait(5000));
        const QString err = spy.first().at(3).toString();
        QVERIFY2(err.contains("401") && err.contains("invalid API key"), qPrintable(err));
        QVERIFY(qvariant_cast<QList<mem0::Hit>>(spy.first().at(1)).isEmpty());

        // Two failures in a row pause recall, so a dead server costs one try per pause.
        c.restAfterFailures = 1;
        spy.clear();
        c.search("x", 2);
        QVERIFY(spy.wait(5000));
        QVERIFY(!c.searchReady());
        QVERIFY(c.searchRestSecondsLeft() > 0);
    }

    void unreachableServerFailsOpen() {
        Mem0Client c;
        c.setEnabled(true);
        c.setConfig("custom", "http://127.0.0.1:1", "k", "noch");   // nothing listens on port 1
        QSignalSpy spy(&c, &Mem0Client::searched);
        c.search("x", 1);
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.first().at(3).toString().isEmpty());
    }

    void providerIsAttachedForThisComputerOnly() {
        FakeServer fake;
        fake.body = R"({"results":[]})";
        Mem0Client c;
        c.setEnabled(true);
        c.setConfig("custom", fake.url(), "k", "noch");
        c.setProviderSupplier([] { return QJsonObject{ { "llm", QJsonObject{ { "api_key", "sk-chat" } } } }; });
        QSignalSpy spy(&c, &Mem0Client::searched);
        c.search("x", 1);
        QVERIFY(spy.wait(5000));
        QCOMPARE(fake.sentJson().value("provider").toObject().value("llm").toObject().value("api_key").toString(),
                 QStringLiteral("sk-chat"));
    }

    // Recall rests on its own failures: a save that succeeds in between must not reset it,
    // and a resting search must not stop saves.
    void searchRestsIndependentlyOfSaves() {
        FakeServer fake;
        fake.searchStatus = 503;                 // searches fail, saves succeed
        fake.body = R"({"detail":"slow"})";

        Mem0Client c;
        c.setEnabled(true);
        c.searchRestMs = 60000;
        c.setConfig("custom", fake.url(), "k", "noch");
        QVERIFY(c.searchReady());

        QSignalSpy searched(&c, &Mem0Client::searched);
        c.search("x", 1);
        QVERIFY(searched.wait(5000));
        c.search("x", 2);
        QVERIFY(searched.wait(5000));
        QVERIFY(!c.searchReady());
        QVERIFY(c.searchRestSecondsLeft() > 0);

        // A save that succeeds in between must not reopen recall.
        QSignalSpy added(&c, &Mem0Client::added);
        c.add({ { "user", "I use Neovim" } }, 3);
        QVERIFY(added.wait(5000));
        QVERIFY(added.first().at(1).toBool());
        QVERIFY(c.ready());
        QVERIFY(!c.searchReady());
    }

    void notReadyWithoutKeyOrWhenOff() {
        Mem0Client c;
        c.setConfig("custom", "http://127.0.0.1:1", "", "noch");
        c.setEnabled(true);
        QVERIFY(!c.ready());               // custom needs a typed key
        c.setConfig("custom", "http://127.0.0.1:1", "k", "noch");
        QVERIFY(c.ready());
        c.setEnabled(false);
        QVERIFY(!c.ready());
    }

    void testReportsAWorkingServer() {
        FakeServer fake;
        fake.body = R"({"results":[]})";
        Mem0Client c;
        c.setConfig("custom", fake.url(), "k", "noch");
        QSignalSpy spy(&c, &Mem0Client::testFinished);
        c.test();
        QVERIFY(spy.wait(5000));
        QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
    }
};

QTEST_MAIN(TstMem0Client)
#include "tst_mem0client.moc"
