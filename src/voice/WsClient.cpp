#include "WsClient.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSslError>

namespace {

const QByteArray kGuid = QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

QByteArray randomBytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        out[i] = char(QRandomGenerator::system()->bounded(256));
    return out;
}

void applyMask(QByteArray &data, const QByteArray &key)
{
    if (key.size() != 4)
        return;
    char *p = data.data();
    const qsizetype n = data.size();
    for (qsizetype i = 0; i < n; ++i)
        p[i] = char(quint8(p[i]) ^ quint8(key[i & 3]));
}

} // namespace

QByteArray WsClient::acceptKey(const QByteArray &clientKey)
{
    return QCryptographicHash::hash(clientKey + kGuid, QCryptographicHash::Sha1).toBase64();
}

QByteArray WsClient::encodeFrame(quint8 opcode, const QByteArray &payload, bool fin, const QByteArray &maskKey)
{
    QByteArray frame;
    const bool masked = maskKey.size() == 4;
    frame.reserve(int(payload.size()) + 14);
    frame.append(char((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
    const quint64 n = quint64(payload.size());
    const char maskBit = masked ? char(0x80) : char(0);
    if (n < 126) {
        frame.append(char(maskBit | char(n)));
    } else if (n <= 0xFFFF) {
        frame.append(char(maskBit | char(126)));
        frame.append(char((n >> 8) & 0xFF));
        frame.append(char(n & 0xFF));
    } else {
        frame.append(char(maskBit | char(127)));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.append(char((n >> shift) & 0xFF));
    }
    if (masked) {
        frame.append(maskKey);
        QByteArray body = payload;
        applyMask(body, maskKey);
        frame.append(body);
    } else {
        frame.append(payload);
    }
    return frame;
}

WsClient::WsClient(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_state == State::Connecting)
            fail(tr("The connection timed out."));
        else if (m_state == State::Closing)
            finish(m_closeCode, m_closeReason, false);
    });
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(&m_socket, &QSslSocket::encrypted, this, &WsClient::onConnected);
    connect(&m_socket, &QSslSocket::connected, this, [this] {
        if (m_url.scheme() != QLatin1String("wss"))
            onConnected();
    });
    connect(&m_socket, &QSslSocket::readyRead, this, &WsClient::onReadyRead);
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this] { onSocketError(); });
    connect(&m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        // Never ignored: a certificate problem ends the connection.
        QString why = errors.isEmpty() ? tr("The secure connection failed.") : errors.first().errorString();
        fail(tr("The secure connection failed: %1").arg(why));
    });
    connect(&m_socket, &QSslSocket::disconnected, this, &WsClient::onDisconnected);
}

WsClient::~WsClient()
{
    abort();
}

void WsClient::open(const QUrl &url, const QList<Header> &headers, const QStringList &subprotocols)
{
    abort();
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || (scheme != QLatin1String("ws") && scheme != QLatin1String("wss"))) {
        m_finished = false;
        m_state = State::Connecting;
        fail(tr("That is not a WebSocket address."));
        return;
    }
    m_url = url;
    m_url.setScheme(scheme);
    m_headers = headers;
    m_subprotocols = subprotocols;
    m_key = randomBytes(16).toBase64();
    m_inbox.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
    m_subprotocol.clear();
    m_closeSent = false;
    m_closeCode = 1005;
    m_closeReason.clear();
    m_finished = false;
    m_state = State::Connecting;
    m_timeout.start(kHandshakeTimeoutMs);
    const quint16 port = quint16(m_url.port(scheme == QLatin1String("wss") ? 443 : 80));
    if (scheme == QLatin1String("wss"))
        m_socket.connectToHostEncrypted(m_url.host(), port);
    else
        m_socket.connectToHost(m_url.host(), port);
}

void WsClient::onConnected()
{
    if (m_state != State::Connecting)
        return;
    QByteArray path = m_url.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty())
        path = "/";
    if (m_url.hasQuery())
        path += '?' + m_url.query(QUrl::FullyEncoded).toUtf8();
    QByteArray host = m_url.host().toUtf8();
    const int defaultPort = m_url.scheme() == QLatin1String("wss") ? 443 : 80;
    if (m_url.port(defaultPort) != defaultPort)
        host += ':' + QByteArray::number(m_url.port());
    QByteArray req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Key: " + m_key + "\r\nSec-WebSocket-Version: 13\r\n";
    if (!m_subprotocols.isEmpty())
        req += "Sec-WebSocket-Protocol: " + m_subprotocols.join(QStringLiteral(", ")).toLatin1() + "\r\n";
    for (const Header &h : std::as_const(m_headers)) {
        // A header value can never carry a line break (header injection).
        if (h.first.contains('\n') || h.first.contains('\r') || h.second.contains('\n') || h.second.contains('\r'))
            continue;
        req += h.first + ": " + h.second + "\r\n";
    }
    req += "\r\n";
    m_socket.write(req);
}

void WsClient::onReadyRead()
{
    if (m_finished)
        return;
    m_inbox += m_socket.readAll();
    if (m_state == State::Connecting) {
        if (!parseHandshake())
            return;
    }
    if (m_state == State::Open || m_state == State::Closing)
        parseFrames();
}

bool WsClient::parseHandshake()
{
    const int end = m_inbox.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_inbox.size() > 64 * 1024)
            fail(tr("The server's answer was not understood."));
        return false;
    }
    const QByteArray head = m_inbox.left(end);
    const QByteArray body = m_inbox.mid(end + 4);
    m_inbox = body;
    const QList<QByteArray> lines = head.split('\n');
    const QByteArray statusLine = lines.value(0).trimmed();
    const QList<QByteArray> status = statusLine.split(' ');
    const int code = status.size() >= 2 ? status.at(1).toInt() : 0;
    QHash<QByteArray, QByteArray> fields;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0)
            fields.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    if (code != 101) {
        // Servers explain a refusal in the body (Deepgram: a JSON err_msg).
        QString detail = QString::fromUtf8(body).simplified().left(240);
        fail(code > 0 ? (detail.isEmpty() ? tr("The server refused the connection (HTTP %1).").arg(code)
                                          : tr("The server refused the connection (HTTP %1): %2").arg(code).arg(detail))
                      : tr("The server's answer was not understood."));
        return false;
    }
    if (fields.value("upgrade").toLower() != "websocket" ||
        !fields.value("connection").toLower().contains("upgrade") ||
        fields.value("sec-websocket-accept") != acceptKey(m_key)) {
        fail(tr("The server did not complete the WebSocket handshake."));
        return false;
    }
    m_subprotocol = QString::fromLatin1(fields.value("sec-websocket-protocol"));
    m_timeout.stop();
    m_state = State::Open;
    emit opened();
    return true;
}

void WsClient::parseFrames()
{
    while (!m_finished) {
        const qsizetype avail = m_inbox.size();
        if (avail < 2)
            return;
        const quint8 b0 = quint8(m_inbox[0]);
        const quint8 b1 = quint8(m_inbox[1]);
        const bool fin = b0 & 0x80;
        const quint8 opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        quint64 len = b1 & 0x7F;
        qsizetype pos = 2;
        if ((b0 & 0x70) != 0) {                      // reserved bits: no extension was negotiated
            fail(tr("The server sent an invalid message."), 1002);
            return;
        }
        if (len == 126) {
            if (avail < 4)
                return;
            len = (quint64(quint8(m_inbox[2])) << 8) | quint8(m_inbox[3]);
            pos = 4;
        } else if (len == 127) {
            if (avail < 10)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | quint8(m_inbox[2 + i]);
            pos = 10;
        }
        if (len > quint64(kMaxMessageBytes)) {
            fail(tr("The server sent a message that is too large."), 1009);
            return;
        }
        QByteArray maskKey;
        if (masked) {
            if (avail < pos + 4)
                return;
            maskKey = m_inbox.mid(pos, 4);
            pos += 4;
        }
        if (quint64(avail - pos) < len)
            return;
        QByteArray payload = m_inbox.mid(pos, qsizetype(len));
        m_inbox.remove(0, pos + qsizetype(len));
        if (masked)
            applyMask(payload, maskKey);
        handleFrame(opcode, fin, payload);
    }
}

void WsClient::handleFrame(quint8 opcode, bool fin, const QByteArray &payload)
{
    switch (opcode) {
    case Ping:
        sendFrame(Pong, payload.left(125));
        return;
    case Pong:
        return;
    case Close: {
        quint16 code = 1005;
        QString reason;
        if (payload.size() >= 2) {
            code = quint16((quint8(payload[0]) << 8) | quint8(payload[1]));
            reason = QString::fromUtf8(payload.mid(2));
        }
        if (!m_closeSent) {
            m_closeSent = true;
            sendFrame(Close, payload.left(2));
        }
        finish(code, reason, true);
        return;
    }
    case Text:
    case Binary:
        if (m_fragmentOpcode != 0) {
            fail(tr("The server sent an invalid message."), 1002);
            return;
        }
        m_fragmentOpcode = opcode;
        m_fragment = payload;
        break;
    case Continuation:
        if (m_fragmentOpcode == 0) {
            fail(tr("The server sent an invalid message."), 1002);
            return;
        }
        if (m_fragment.size() + payload.size() > kMaxMessageBytes) {
            fail(tr("The server sent a message that is too large."), 1009);
            return;
        }
        m_fragment += payload;
        break;
    default:
        fail(tr("The server sent an invalid message."), 1002);
        return;
    }
    if (fin)
        deliverMessage();
}

void WsClient::deliverMessage()
{
    const quint8 opcode = m_fragmentOpcode;
    QByteArray data = std::exchange(m_fragment, QByteArray());
    m_fragmentOpcode = 0;
    if (m_state != State::Open)
        return;      // closing: anything still arriving is dropped
    if (opcode == Text)
        emit textReceived(QString::fromUtf8(data));
    else
        emit binaryReceived(data);
}

void WsClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    m_socket.write(encodeFrame(opcode, payload, true, randomBytes(4)));
}

bool WsClient::sendText(const QString &text)
{
    if (m_state != State::Open)
        return false;
    sendFrame(Text, text.toUtf8());
    return true;
}

bool WsClient::sendBinary(const QByteArray &data)
{
    if (m_state != State::Open)
        return false;
    sendFrame(Binary, data);
    return true;
}

void WsClient::close(quint16 code, const QString &reason)
{
    if (m_state == State::Connecting) {
        abort();
        return;
    }
    if (m_state != State::Open)
        return;
    m_state = State::Closing;
    m_closeCode = code;
    m_closeReason = reason;
    m_closeSent = true;
    QByteArray payload;
    payload.append(char(code >> 8));
    payload.append(char(code & 0xFF));
    payload.append(reason.toUtf8().left(123));
    sendFrame(Close, payload);
    m_timeout.start(kCloseTimeoutMs);
}

void WsClient::abort()
{
    m_finished = true;
    m_timeout.stop();
    m_state = State::Closed;
    m_socket.abort();
    m_inbox.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
}

void WsClient::onSocketError()
{
    if (m_finished)
        return;
    const QString why = m_socket.errorString();
    if (m_state == State::Connecting)
        fail(tr("Could not connect: %1").arg(why));
    else if (m_state == State::Closing)
        finish(m_closeCode, m_closeReason, false);
    else
        fail(tr("The connection was lost: %1").arg(why));
}

void WsClient::onDisconnected()
{
    if (m_finished)
        return;
    if (m_state == State::Closing)
        finish(m_closeCode, m_closeReason, false);
    else if (m_state == State::Connecting)
        fail(tr("The server closed the connection."));
    else
        fail(tr("The connection was lost."));
}

void WsClient::fail(const QString &message, quint16 code)
{
    if (m_finished)
        return;
    emit errorOccurred(message);
    finish(code, message, false);
}

void WsClient::finish(quint16 code, const QString &reason, bool clean)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout.stop();
    m_state = State::Closed;
    m_inbox.clear();
    m_fragment.clear();
    m_fragmentOpcode = 0;
    // A close frame we just sent still has to reach the server: disconnectFromHost()
    // writes what is pending first, abort() would throw it away.
    if (clean && m_socket.state() == QAbstractSocket::ConnectedState)
        m_socket.disconnectFromHost();
    else
        m_socket.abort();
    emit closed(code, reason, clean);
}
