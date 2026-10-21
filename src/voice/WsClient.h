#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

// A small WebSocket client (RFC 6455) on top of QSslSocket. Qt's own
// QtWebSockets module is not part of every Qt install, and Noch only needs the
// client side: connect (ws:// or wss://), send text and binary messages, receive
// them, and close politely.
//
// Handled: the opening handshake (including the Sec-WebSocket-Accept check and
// a readable error when the server answers 401/403/…), client-side masking,
// fragmented messages, ping/pong, the close handshake, and a cap on message
// size so a hostile server cannot make the app allocate without bound.
// Not handled (not needed): extensions such as permessage-deflate, and
// reconnecting; the owner decides when to open a new connection.
class WsClient : public QObject
{
    Q_OBJECT
public:
    enum class State { Closed, Connecting, Open, Closing };
    using Header = QPair<QByteArray, QByteArray>;

    static constexpr qint64 kMaxMessageBytes = 16 * 1024 * 1024;
    static constexpr int kHandshakeTimeoutMs = 10000;
    static constexpr int kCloseTimeoutMs = 2000;

    explicit WsClient(QObject *parent = nullptr);
    ~WsClient() override;

    State state() const { return m_state; }
    bool isOpen() const { return m_state == State::Open; }
    // Bytes accepted by send*() that the network has not taken yet; callers that
    // stream audio use it to drop frames instead of building a backlog.
    qint64 bufferedBytes() const { return m_socket.bytesToWrite(); }

    // Starts connecting. `headers` are added to the upgrade request (for example
    // Authorization); `subprotocols` go in Sec-WebSocket-Protocol. A connection
    // that is already open or opening is dropped first. TLS errors are never
    // ignored.
    void open(const QUrl &url, const QList<Header> &headers = {}, const QStringList &subprotocols = {});
    bool sendText(const QString &text);
    bool sendBinary(const QByteArray &data);
    // Starts the close handshake; closed() follows (a moment later at most).
    void close(quint16 code = 1000, const QString &reason = QString());
    // Drops the connection at once; no closed() signal.
    void abort();

    // The subprotocol the server selected ("" if none).
    QString subprotocol() const { return m_subprotocol; }

    // ---- pure helpers (unit tested) ----------------------------------------------
    static QByteArray acceptKey(const QByteArray &clientKey);
    // One frame as it goes on the wire. `maskKey` empty = unmasked (server side, used by tests).
    static QByteArray encodeFrame(quint8 opcode, const QByteArray &payload, bool fin = true,
                                  const QByteArray &maskKey = QByteArray());

signals:
    void opened();
    void textReceived(const QString &text);
    void binaryReceived(const QByteArray &data);
    // `clean`: the close handshake finished (or the server closed with a code).
    void closed(quint16 code, const QString &reason, bool clean);
    // A failure to connect or a broken stream; closed() follows when it ends the connection.
    void errorOccurred(const QString &message);

private:
    enum Opcode : quint8 { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();
    bool parseHandshake();
    void parseFrames();
    void handleFrame(quint8 opcode, bool fin, const QByteArray &payload);
    void deliverMessage();
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void fail(const QString &message, quint16 code = 1006);
    void finish(quint16 code, const QString &reason, bool clean);

    QSslSocket m_socket;
    QTimer m_timeout;
    State m_state = State::Closed;
    QUrl m_url;
    QList<Header> m_headers;
    QStringList m_subprotocols;
    QByteArray m_key;
    QByteArray m_inbox;
    QString m_subprotocol;
    // Message being assembled from fragments.
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragment;
    // The close frame we sent or got.
    bool m_closeSent = false;
    quint16 m_closeCode = 1005;
    QString m_closeReason;
    bool m_finished = true;
};
