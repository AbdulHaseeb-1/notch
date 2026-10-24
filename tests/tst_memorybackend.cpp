#include <QtTest>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include "LocalMemoryBackend.h"
#include "Mem0MemoryBackend.h"
#include "MemoryBackend.h"

namespace {

MemoryHit hit(const QString &backend, const QString &text, double rank)
{
    MemoryHit h;
    h.backend = backend;
    h.text = text;
    h.score = rank;
    h.rank = rank;
    return h;
}

// A minimal Mem0-shaped HTTP server: searches answer `searchBody` after
// `searchDelayMs`, adds answer success. Records what it saw.
struct FakeMem0Server {
    QTcpServer server;
    QByteArray searchBody = "{\"results\":[{\"id\":\"m1\",\"memory\":\"User's editor is Neovim\",\"score\":0.9}]}";
    int searchDelayMs = 0;
    int searches = 0;
    int adds = 0;
    QByteArray lastAddBody;

    bool start()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = QSharedPointer<QByteArray>::create();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    // Qt 6.9's QNetworkAccessManager sends header names
                    // lowercase ("content-length:"); parse case-insensitively
                    // or the body length reads as 0 and every recorded body
                    // comes out empty (the pre-existing FakeMem0 bug).
                    const int len = QString::fromLatin1(buf->left(end)).toLower().section("content-length:", 1)
                                        .section("\r\n", 0, 0).trimmed().toInt();
                    if (buf->size() < end + 4 + len)
                        return;
                    const bool isAdd = buf->startsWith("POST /v3/memories/add/");
                    const QByteArray reply = isAdd ? QByteArray("{\"status\":\"SUCCEEDED\",\"results\":[]}")
                                                   : searchBody;
                    if (isAdd) {
                        ++adds;
                        lastAddBody = buf->mid(end + 4, len);
                    }
                    else
                        ++searches;
                    buf->clear();
                    const QByteArray out = "HTTP/1.1 200 X\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply;
                    if (!isAdd && searchDelayMs > 0) {
                        QPointer<QTcpSocket> sp(s);
                        QTimer::singleShot(searchDelayMs, s, [sp, out] {
                            if (!sp)
                                return;
                            sp->write(out);
                            sp->disconnectFromHost();
                        });
                    } else {
                        s->write(out);
                        s->disconnectFromHost();
                    }
                });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
};

} // namespace

class TstMemoryBackend : public QObject
{
    Q_OBJECT
private slots:
    // ---- mergeMemoryHits -------------------------------------------------

    void mergeDedupesAcrossSourcesKeepingHigherRank()
    {
        const QList<MemoryHit> local{ hit("local", "User's editor is Neovim", 0.4) };
        const QList<MemoryHit> remote{ hit("mem0", "User's editor is Neovim", 0.9) };
        const QList<MemoryHit> merged = mergeMemoryHits(local, remote, 14);
        QCOMPARE(merged.size(), 1);
        QCOMPARE(merged.first().backend, QStringLiteral("mem0"));   // higher rank wins
        QCOMPARE(merged.first().rank, 0.9);
    }

    void mergeDedupeIgnoresCaseAndWhitespaceAndTieKeepsLocal()
    {
        const QList<MemoryHit> local{ hit("local", "likes   dark\nthemes", 0.5) };
        const QList<MemoryHit> remote{ hit("mem0", "Likes dark themes", 0.5) };
        const QList<MemoryHit> merged = mergeMemoryHits(local, remote, 14);
        QCOMPARE(merged.size(), 1);
        QCOMPARE(merged.first().backend, QStringLiteral("local"));   // tie: local (passed first) wins
    }

    void mergeRanksDescendingAndCaps()
    {
        const QList<MemoryHit> local{ hit("local", "a", 0.2), hit("local", "b", 0.8) };
        const QList<MemoryHit> remote{ hit("mem0", "c", 0.5), hit("mem0", "d", 0.1) };
        const QList<MemoryHit> merged = mergeMemoryHits(local, remote, 3);
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged.at(0).text, QStringLiteral("b"));
        QCOMPARE(merged.at(1).text, QStringLiteral("c"));
        QCOMPARE(merged.at(2).text, QStringLiteral("a"));
        QCOMPARE(mergeMemoryHits(local, remote, 0).size(), 4);   // cap <= 0: no cap
        QVERIFY(mergeMemoryHits({}, {}, 5).isEmpty());
    }

    void mergeRankIsStableForEqualRanks()
    {
        const QList<MemoryHit> local{ hit("local", "first", 0.5), hit("local", "second", 0.5) };
        const QList<MemoryHit> remote{ hit("mem0", "third", 0.5) };
        const QList<MemoryHit> merged = mergeMemoryHits(local, remote, 10);
        QCOMPARE(merged.at(0).text, QStringLiteral("first"));
        QCOMPARE(merged.at(1).text, QStringLiteral("second"));
        QCOMPARE(merged.at(2).text, QStringLiteral("third"));
    }

    // ---- renderers stay byte-identical to the Mem0 helpers ---------------

    void promptSectionMatchesMem0Helper()
    {
        const QList<mem0::Hit> raw{ { "m1", "User's editor\nis Neovim", 0.9 }, { "m2", "Likes tea", 0.5 } };
        QList<MemoryHit> hits;
        for (const mem0::Hit &h : raw) {
            MemoryHit mh = hit("mem0", h.memory, h.score);
            mh.id = h.id;
            hits.append(mh);
        }
        QCOMPARE(memoryPromptSection(hits), mem0::recallSection(raw));
        QCOMPARE(memoryRecallLines(hits), mem0::recallLines(raw));
        QCOMPARE(memoryPromptSection({}), mem0::recallSection({}));
    }

    // ---- LocalMemoryBackend ----------------------------------------------

    void localRecallIsSynchronousAndRanked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LocalMemoryBackend backend([&] { return dir.path(); });
        QCOMPARE(backend.id(), QStringLiteral("local"));
        QVERIFY(backend.recallReady());
        QVERIFY(!backend.saveReady());   // facts, not exchanges

        const auto counts = backend.addFacts({ QStringLiteral("My editor is Neovim and I use it daily"),
                                               QStringLiteral("My cat is called Pixel") });
        QCOMPARE(counts.added, 2);

        bool fired = false;
        QList<MemoryHit> got;
        backend.recall(QStringLiteral("which editor do I use"), 5, 42,
                       [&](quint64 token, const QList<MemoryHit> &hits, const QString &error, int ms) {
                           fired = true;
                           QCOMPARE(token, quint64(42));
                           QVERIFY(error.isEmpty());
                           QCOMPARE(ms, 0);
                           got = hits;
                       });
        QVERIFY(fired);   // synchronous: fired before recall() returned
        QVERIFY(!got.isEmpty());
        QCOMPARE(got.first().backend, QStringLiteral("local"));
        QVERIFY(got.first().text.contains(QStringLiteral("Neovim")));
        QVERIFY(got.first().rank > 0.0 && got.first().rank < 1.0);   // BM25 squashed into 0..1
        QVERIFY(!got.first().id.isEmpty());
    }

    void localAddFactsUpdatesRestatedFact()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LocalMemoryBackend backend([&] { return dir.path(); });
        QCOMPARE(backend.addFacts({ QStringLiteral("My editor is Neovim") }).added, 1);
        const auto again = backend.addFacts({ QStringLiteral("My editor is Neovim") });
        QCOMPARE(again.added, 0);
        QCOMPARE(again.updated, 1);   // same fact restated: UPDATE, not a duplicate ADD
    }

    void localSnapshotFallsBackToNewestAndReportsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LocalMemoryBackend backend([&] { return dir.path(); });
        bool empty = false, fallback = false;
        QVERIFY(backend.snapshotHits(QStringLiteral("anything"), &empty, &fallback).isEmpty());
        QVERIFY(empty);

        backend.addFacts({ QStringLiteral("The wifi password lives on the fridge"),
                           QStringLiteral("Standup is at nine thirty") });
        const QList<MemoryHit> hits = backend.snapshotHits(QStringLiteral("editor"), &empty, &fallback);
        QVERIFY(!empty);
        QVERIFY(fallback);   // nothing matches "editor": newest facts stand in
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.first().rank, 0.0);
    }

    void localAddExchangeIsHonestlyRefused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LocalMemoryBackend backend([&] { return dir.path(); });
        bool fired = false;
        backend.addExchange({ { QStringLiteral("user"), QStringLiteral("remember this") } }, 7, {},
                            [&](quint64 token, bool ok, const QString &error) {
                                fired = true;
                                QCOMPARE(token, quint64(7));
                                QVERIFY(!ok);
                                QVERIFY(!error.isEmpty());
                            });
        QVERIFY(fired);
    }

    // ---- Mem0MemoryBackend -------------------------------------------------

    void mem0RecallConvertsHits()
    {
        FakeMem0Server server;
        QVERIFY(server.start());
        Mem0Client client;
        client.setEnabled(true);
        client.setConfig(QStringLiteral("custom"), server.base(), QStringLiteral("k"), QStringLiteral("noch"));
        Mem0MemoryBackend backend(&client);
        QCOMPARE(backend.id(), QStringLiteral("mem0"));
        QVERIFY(backend.recallReady());
        QVERIFY(backend.saveReady());

        bool fired = false;
        QList<MemoryHit> got;
        backend.recall(QStringLiteral("which editor"), 6, 3,
                       [&](quint64 token, const QList<MemoryHit> &hits, const QString &error, int) {
                           fired = true;
                           QCOMPARE(token, quint64(3));
                           QVERIFY(error.isEmpty());
                           got = hits;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(fired, 5000);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().backend, QStringLiteral("mem0"));
        QCOMPARE(got.first().text, QStringLiteral("User's editor is Neovim"));
        QCOMPARE(got.first().rank, 0.9);
        QCOMPARE(got.first().id, QStringLiteral("m1"));
    }

    void mem0SearchSupersedesInFlightRecall()
    {
        FakeMem0Server server;
        server.searchDelayMs = 300;
        QVERIFY(server.start());
        Mem0Client client;
        client.setEnabled(true);
        client.setConfig(QStringLiteral("custom"), server.base(), QStringLiteral("k"), QStringLiteral("noch"));
        Mem0MemoryBackend backend(&client);

        int recallFired = 0, searchFired = 0;
        backend.recall(QStringLiteral("q"), 6, 1,
                       [&](quint64, const QList<MemoryHit> &, const QString &, int) { ++recallFired; });
        backend.search(QStringLiteral("q"), 6, 1,
                       [&](quint64, const QList<MemoryHit> &, const QString &, int) { ++searchFired; });
        QTest::qWait(1200);
        QCOMPARE(recallFired, 0);   // superseded: dropped, never fires
        QCOMPARE(searchFired, 1);
    }

    void mem0CancelDropsTheCallback()
    {
        FakeMem0Server server;
        server.searchDelayMs = 300;
        QVERIFY(server.start());
        Mem0Client client;
        client.setEnabled(true);
        client.setConfig(QStringLiteral("custom"), server.base(), QStringLiteral("k"), QStringLiteral("noch"));
        Mem0MemoryBackend backend(&client);

        int fired = 0;
        backend.recall(QStringLiteral("q"), 6, 1,
                       [&](quint64, const QList<MemoryHit> &, const QString &, int) { ++fired; });
        backend.cancelRecall();
        QTest::qWait(900);
        QCOMPARE(fired, 0);
    }

    void mem0AddExchangeReportsBack()
    {
        FakeMem0Server server;
        QVERIFY(server.start());
        Mem0Client client;
        client.setEnabled(true);
        client.setConfig(QStringLiteral("custom"), server.base(), QStringLiteral("k"), QStringLiteral("noch"));
        Mem0MemoryBackend backend(&client);

        bool fired = false;
        backend.addExchange({ { QStringLiteral("user"), QStringLiteral("hi there") },
                              { QStringLiteral("assistant"), QStringLiteral("hello") } },
                            9, {},
                            [&](quint64 token, bool ok, const QString &error) {
                                fired = true;
                                QCOMPARE(token, quint64(9));
                                QVERIFY(ok);
                                QVERIFY(error.isEmpty());
                            });
        QTRY_VERIFY_WITH_TIMEOUT(fired, 5000);
        QCOMPARE(server.adds, 1);
        // The adapter must forward the exchange verbatim: the two
        // messages the provider built are the two the server receives.
        // (This exonerates the adapter for the pre-existing
        // tst_buddyagent save-payload failure, which lives in the
        // untouched save/config path.)
        const QJsonObject sent = QJsonDocument::fromJson(server.lastAddBody).object();
        const QJsonArray msgs = sent.value(QStringLiteral("messages")).toArray();
        QCOMPARE(msgs.size(), 2);
        QCOMPARE(msgs.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        QCOMPARE(msgs.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("hi there"));
        QCOMPARE(msgs.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
        QCOMPARE(sent.value(QStringLiteral("user_id")).toString(), QStringLiteral("noch"));
    }
};

QTEST_MAIN(TstMemoryBackend)
#include "tst_memorybackend.moc"
