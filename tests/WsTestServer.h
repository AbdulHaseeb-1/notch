#pragma once
// A tiny WebSocket server for tests: accepts one connection at a time, does the
// opening handshake (or refuses it as told), records what the client sends and
// lets the test push frames back.
#include <QCryptographicHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include "WsClient.h"

class WsTestServer : public QObject
{
    Q_OBJECT
public:
    explicit WsTestServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &WsTestServer::onConnection);
        m_server.listen(QHostAddress::LocalHost, 0);
    }
    QString url(const QString &path = QStringLiteral("/")) const
    {
        return QStringLiteral("ws://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }

    // ---- behaviour the test can change before connecting ---------------------------
    int refuseStatus = 0;                 // e.g. 401: answer the upgrade with this HTTP status
    QByteArray refuseBody;
    bool badAccept = false;               // answer with a wrong Sec-WebSocket-Accept
    QByteArray selectProtocol;            // echoed back as Sec-WebSocket-Protocol

    // ---- what happened ----------------------------------------------------------------
    QByteArray request;                   // the client's upgrade request, headers and all
    QStringList texts;
    QList<QByteArray> binaries;
    bool allMasked = true;                // every client frame carried a mask
    quint16 closeCode = 0;
    bool clientClosed = false;
    int connections = 0;
    bool connectedNow() const { return m_sock && m_sock->state() == QAbstractSocket::ConnectedState && m_upgraded; }
    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : request.split('\n')) {
            const int c = line.indexOf(':');
            if (c > 0 && line.left(c).trimmed().toLower() == name.toLower())
                return line.mid(c + 1).trimmed();
        }
        return {};
    }

    // ---- sending -----------------------------------------------------------------------
    void sendText(const QString &t) { raw(WsClient::encodeFrame(0x1, t.toUtf8())); }
    void sendBinary(const QByteArray &b) { raw(WsClient::encodeFrame(0x2, b)); }
    void sendPing(const QByteArray &b = QByteArray("p")) { raw(WsClient::encodeFrame(0x9, b)); }
    void sendFrame(quint8 opcode, const QByteArray &payload, bool fin = true) { raw(WsClient::encodeFrame(opcode, payload, fin)); }
    void sendClose(quint16 code, const QString &reason = QString())
    {
        QByteArray p;
        p.append(char(code >> 8));
        p.append(char(code & 0xFF));
        p.append(reason.toUtf8());
        raw(WsClient::encodeFrame(0x8, p));
    }
    void raw(const QByteArray &bytes)
    {
        if (m_sock)
            m_sock->write(bytes);
    }
    void drop()
    {
        if (m_sock)
            m_sock->abort();
    }

signals:
    void upgraded();
    void textReceived(const QString &text);
    void binaryReceived(const QByteArray &data);
    void pongReceived(const QByteArray &payload);
    void clientGone();

private:
    void onConnection()
    {
        m_sock = m_server.nextPendingConnection();
        ++connections;
        m_buf.clear();
        m_upgraded = false;
        m_message.clear();
        connect(m_sock, &QTcpSocket::readyRead, this, &WsTestServer::onData);
        connect(m_sock, &QTcpSocket::disconnected, this, [this] { emit clientGone(); });
    }

    void onData()
    {
        m_buf += m_sock->readAll();
        if (!m_upgraded) {
            const int end = m_buf.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            request = m_buf.left(end);
            m_buf.remove(0, end + 4);
            if (refuseStatus) {
                m_sock->write("HTTP/1.1 " + QByteArray::number(refuseStatus) + " Refused\r\nContent-Type: application/json\r\n"
                              "Content-Length: " + QByteArray::number(refuseBody.size()) + "\r\nConnection: close\r\n\r\n" + refuseBody);
                m_sock->disconnectFromHost();
                return;
            }
            const QByteArray key = header("Sec-WebSocket-Key");
            QByteArray accept = WsClient::acceptKey(key);
            if (badAccept)
                accept = "AAAAAAAAAAAAAAAAAAAAAAAAAAA=";
            QByteArray resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                              "Sec-WebSocket-Accept: " + accept + "\r\n";
            if (!selectProtocol.isEmpty())
                resp += "Sec-WebSocket-Protocol: " + selectProtocol + "\r\n";
            resp += "\r\n";
            m_sock->write(resp);
            m_upgraded = true;
            emit upgraded();
        }
        parse();
    }

    void parse()
    {
        while (m_buf.size() >= 2) {
            const quint8 b0 = quint8(m_buf[0]), b1 = quint8(m_buf[1]);
            const bool fin = b0 & 0x80;
            const quint8 op = b0 & 0x0F;
            const bool masked = b1 & 0x80;
            quint64 len = b1 & 0x7F;
            int pos = 2;
            if (len == 126) { if (m_buf.size() < 4) return; len = (quint64(quint8(m_buf[2])) << 8) | quint8(m_buf[3]); pos = 4; }
            else if (len == 127) {
                if (m_buf.size() < 10) return;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | quint8(m_buf[2 + i]);
                pos = 10;
            }
            QByteArray mask;
            if (masked) { if (m_buf.size() < pos + 4) return; mask = m_buf.mid(pos, 4); pos += 4; }
            if (quint64(m_buf.size() - pos) < len)
                return;
            QByteArray payload = m_buf.mid(pos, int(len));
            m_buf.remove(0, pos + int(len));
            if (!masked)
                allMasked = false;
            else
                for (int i = 0; i < payload.size(); ++i)
                    payload[i] = char(quint8(payload[i]) ^ quint8(mask[i & 3]));
            switch (op) {
            case 0x9: raw(WsClient::encodeFrame(0xA, payload)); break;
            case 0xA: emit pongReceived(payload); break;
            case 0x8:
                clientClosed = true;
                closeCode = payload.size() >= 2 ? quint16((quint8(payload[0]) << 8) | quint8(payload[1])) : 1005;
                raw(WsClient::encodeFrame(0x8, payload.left(2)));
                break;
            case 0x1:
            case 0x2:
            case 0x0:
                if (op != 0x0)
                    m_msgOp = op;
                m_message += payload;
                if (fin) {
                    if (m_msgOp == 0x1) { texts << QString::fromUtf8(m_message); emit textReceived(texts.last()); }
                    else { binaries << m_message; emit binaryReceived(m_message); }
                    m_message.clear();
                }
                break;
            }
        }
    }

    QTcpServer m_server;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_buf, m_message;
    quint8 m_msgOp = 0;
    bool m_upgraded = false;
};
