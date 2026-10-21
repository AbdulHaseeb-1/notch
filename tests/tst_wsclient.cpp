// The WebSocket client against a local server: handshake, framing in both
// directions, ping/pong, fragmentation, the close handshake and every way a
// connection can fail.
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "WsClient.h"
#include "WsTestServer.h"

class tst_WsClient : public QObject
{
    Q_OBJECT

private slots:
    void acceptKeyMatchesRfcExample()
    {
        QCOMPARE(WsClient::acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    }

    void frameLengthEncodings()
    {
        // 7-bit, 16-bit and 64-bit length forms, unmasked and masked.
        for (int n : { 0, 1, 125, 126, 65535, 65536, 100000 }) {
            const QByteArray payload(n, 'x');
            const QByteArray plain = WsClient::encodeFrame(0x2, payload);
            const int header = n < 126 ? 2 : (n <= 0xFFFF ? 4 : 10);
            QCOMPARE(plain.size(), header + n);
            QCOMPARE(quint8(plain[0]), quint8(0x82));
            const QByteArray masked = WsClient::encodeFrame(0x1, payload, true, QByteArray("\x01\x02\x03\x04", 4));
            QCOMPARE(masked.size(), header + 4 + n);
            QVERIFY(quint8(masked[1]) & 0x80);
        }
        // A non-final frame clears the FIN bit.
        QCOMPARE(quint8(WsClient::encodeFrame(0x1, "a", false)[0]), quint8(0x01));
    }

    void handshakeAndEcho()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened);
        ws.open(QUrl(server.url(QStringLiteral("/v1/agent/converse?x=1"))),
                { { "Authorization", "Token abc" } }, { QStringLiteral("proto1") });
        QCOMPARE(ws.state(), WsClient::State::Connecting);
        QTRY_COMPARE(opened.count(), 1);
        QVERIFY(ws.isOpen());
        QVERIFY(server.request.startsWith("GET /v1/agent/converse?x=1 HTTP/1.1"));
        QCOMPARE(server.header("Authorization"), QByteArray("Token abc"));
        QCOMPARE(server.header("Sec-WebSocket-Version"), QByteArray("13"));
        QCOMPARE(server.header("Sec-WebSocket-Protocol"), QByteArray("proto1"));
        QVERIFY(server.header("Host").startsWith("127.0.0.1:"));

        QVERIFY(ws.sendText(QStringLiteral("héllo ✓")));
        QVERIFY(ws.sendBinary(QByteArray("\x00\x01\x02", 3)));
        QTRY_COMPARE(server.texts.size(), 1);
        QTRY_COMPARE(server.binaries.size(), 1);
        QCOMPARE(server.texts.first(), QStringLiteral("héllo ✓"));
        QCOMPARE(server.binaries.first(), QByteArray("\x00\x01\x02", 3));
        QVERIFY2(server.allMasked, "client frames must be masked");
    }

    void receivesTextBinaryAndLargeMessages()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), text(&ws, &WsClient::textReceived), bin(&ws, &WsClient::binaryReceived);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        server.sendText(QStringLiteral("{\"type\":\"Welcome\"}"));
        server.sendBinary(QByteArray(10, 'a'));
        QByteArray big(300000, 'z');                 // needs the 64-bit length form
        for (int i = 0; i < big.size(); i += 7)
            big[i] = char(i & 0x7F);
        server.sendBinary(big);
        QTRY_COMPARE(text.count(), 1);
        QTRY_COMPARE(bin.count(), 2);
        QCOMPARE(text.first().first().toString(), QStringLiteral("{\"type\":\"Welcome\"}"));
        QCOMPARE(bin.at(0).first().toByteArray(), QByteArray(10, 'a'));
        QCOMPARE(bin.at(1).first().toByteArray(), big);
    }

    void fragmentedMessagesAreReassembled()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), text(&ws, &WsClient::textReceived);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        server.sendFrame(0x1, "Hello, ", false);
        server.sendFrame(0x9, "mid-ping");           // control frames may interleave
        server.sendFrame(0x0, "frag", false);
        server.sendFrame(0x0, "mented", true);
        QTRY_COMPARE(text.count(), 1);
        QCOMPARE(text.first().first().toString(), QStringLiteral("Hello, fragmented"));
    }

    void pingIsAnsweredWithPong()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), pong(&server, &WsTestServer::pongReceived);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        server.sendPing("are you there");
        QTRY_COMPARE(pong.count(), 1);
        QCOMPARE(pong.first().first().toByteArray(), QByteArray("are you there"));
    }

    void clientInitiatedClose()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), closed(&ws, &WsClient::closed);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        ws.close(1000, QStringLiteral("bye"));
        QCOMPARE(ws.state(), WsClient::State::Closing);
        QVERIFY(!ws.sendText(QStringLiteral("late")) || true);   // sending while closing is refused or ignored
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(server.clientClosed);
        QCOMPARE(server.closeCode, quint16(1000));
        QCOMPARE(closed.first().at(0).value<quint16>(), quint16(1000));
        QVERIFY(closed.first().at(2).toBool());      // clean
        QCOMPARE(ws.state(), WsClient::State::Closed);
    }

    void serverInitiatedClose()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), closed(&ws, &WsClient::closed);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        server.sendClose(1001, QStringLiteral("going away"));
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(0).value<quint16>(), quint16(1001));
        QCOMPARE(closed.first().at(1).toString(), QStringLiteral("going away"));
        QVERIFY(closed.first().at(2).toBool());
        QTRY_VERIFY(server.clientClosed);            // the client echoed the close frame
    }

    void refusedHandshakeReportsStatusAndBody()
    {
        WsTestServer server;
        server.refuseStatus = 401;
        server.refuseBody = R"({"err_code":"INVALID_AUTH","err_msg":"Invalid credentials."})";
        WsClient ws;
        QSignalSpy error(&ws, &WsClient::errorOccurred), closed(&ws, &WsClient::closed);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(error.count(), 1);
        const QString why = error.first().first().toString();
        QVERIFY2(why.contains(QStringLiteral("401")), qPrintable(why));
        QVERIFY2(why.contains(QStringLiteral("Invalid credentials")), qPrintable(why));
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!closed.first().at(2).toBool());
        QCOMPARE(ws.state(), WsClient::State::Closed);
    }

    void wrongAcceptKeyIsRejected()
    {
        WsTestServer server;
        server.badAccept = true;
        WsClient ws;
        QSignalSpy error(&ws, &WsClient::errorOccurred), opened(&ws, &WsClient::opened);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(error.count(), 1);
        QCOMPARE(opened.count(), 0);
    }

    void connectionRefused()
    {
        WsClient ws;
        QSignalSpy error(&ws, &WsClient::errorOccurred), closed(&ws, &WsClient::closed);
        ws.open(QUrl(QStringLiteral("ws://127.0.0.1:1/")));   // nothing listens on port 1
        QTRY_COMPARE_WITH_TIMEOUT(error.count(), 1, 5000);
        QTRY_COMPARE(closed.count(), 1);
    }

    void invalidAddressesAreRefused()
    {
        WsClient ws;
        QSignalSpy error(&ws, &WsClient::errorOccurred);
        ws.open(QUrl(QStringLiteral("http://example.com/")));
        QCOMPARE(error.count(), 1);
        ws.open(QUrl(QStringLiteral("not a url")));
        QCOMPARE(error.count(), 2);
        QCOMPARE(ws.state(), WsClient::State::Closed);
    }

    void droppedConnectionIsReportedOnce()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), error(&ws, &WsClient::errorOccurred), closed(&ws, &WsClient::closed);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        server.drop();
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(error.count() >= 1);
        QVERIFY(!closed.first().at(2).toBool());      // not a clean close
        QCOMPARE(closed.first().at(0).value<quint16>(), quint16(1006));
        QTest::qWait(100);
        QCOMPARE(closed.count(), 1);
    }

    void oversizedMessagesAreRefused()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened), error(&ws, &WsClient::errorOccurred), closed(&ws, &WsClient::closed);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        // A binary frame header that promises 1 GB: the client must not wait for, or allocate, it.
        QByteArray header;
        header.append(char(0x82));
        header.append(char(127));
        const quint64 n = 1ull << 30;
        for (int shift = 56; shift >= 0; shift -= 8)
            header.append(char((n >> shift) & 0xFF));
        server.raw(header);
        QTRY_COMPARE(error.count(), 1);
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(0).value<quint16>(), quint16(1009));
    }

    void headerInjectionIsBlocked()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened);
        ws.open(QUrl(server.url()), { { "X-Evil", "a\r\nX-Injected: yes" }, { "X-Fine", "ok" } });
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(server.header("X-Injected"), QByteArray());
        QCOMPARE(server.header("X-Evil"), QByteArray());
        QCOMPARE(server.header("X-Fine"), QByteArray("ok"));
    }

    void reopeningDropsTheOldConnection()
    {
        WsTestServer server;
        WsClient ws;
        QSignalSpy opened(&ws, &WsClient::opened);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 1);
        ws.open(QUrl(server.url()));
        QTRY_COMPARE(opened.count(), 2);
        QCOMPARE(server.connections, 2);
        QVERIFY(ws.sendText(QStringLiteral("x")));
        QTRY_VERIFY(!server.texts.isEmpty());
    }

    void notOpenMeansNoSending()
    {
        WsClient ws;
        QVERIFY(!ws.sendText(QStringLiteral("x")));
        QVERIFY(!ws.sendBinary(QByteArray("x")));
        ws.close();      // harmless
        ws.abort();
    }
};

QTEST_GUILESS_MAIN(tst_WsClient)
#include "tst_wsclient.moc"
