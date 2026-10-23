#pragma once
// A tiny scripted HTTP server on 127.0.0.1 for the connector tests: the handler sees
// each request (method, path, query, headers, body) and answers it; every request is
// recorded. One request per connection, then the connection is closed.
#include <QHash>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

struct HttpRequest {
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;   // names lower-cased
    QByteArray body;

    QUrl url() const { return QUrl::fromEncoded("http://127.0.0.1" + target); }
    QString path() const { return url().path(); }
    QUrlQuery query() const { return QUrlQuery(url()); }
    QByteArray header(const char *name) const { return headers.value(QByteArray(name).toLower()); }
};

struct HttpResponse {
    int status = 200;
    QByteArray body;
    QByteArray contentType = "application/json";
    QList<QPair<QByteArray, QByteArray>> headers = {};

    static HttpResponse json(const QByteArray &body, int status = 200) { return { status, body }; }
    static HttpResponse text(int status, const QByteArray &body) { return { status, body, "text/plain" }; }
};

struct FakeHttpServer {
    QTcpServer server;
    QList<HttpRequest> requests;
    std::function<HttpResponse(const HttpRequest &)> handler;

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = QSharedPointer<QByteArray>::create();
                QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    HttpRequest r;
                    const QList<QByteArray> lines = buf->left(end).split('\n');
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    r.method = first.value(0);
                    r.target = first.value(1);
                    int len = 0;
                    for (int i = 1; i < lines.size(); ++i) {
                        const int colon = lines.at(i).indexOf(':');
                        if (colon <= 0)
                            continue;
                        const QByteArray name = lines.at(i).left(colon).trimmed().toLower();
                        const QByteArray value = lines.at(i).mid(colon + 1).trimmed();
                        r.headers.insert(name, value);
                        if (name == "content-length")
                            len = value.toInt();
                    }
                    if (buf->size() < end + 4 + len)
                        return;
                    r.body = buf->mid(end + 4, len);
                    buf->clear();
                    requests.append(r);
                    const HttpResponse resp = handler ? handler(r) : HttpResponse::text(404, "no handler");
                    QByteArray out = "HTTP/1.1 " + QByteArray::number(resp.status) + " X\r\nContent-Type: " + resp.contentType
                                   + "\r\nContent-Length: " + QByteArray::number(resp.body.size()) + "\r\nConnection: close\r\n";
                    for (const auto &h : resp.headers)
                        out += h.first + ": " + h.second + "\r\n";
                    s->write(out + "\r\n" + resp.body);
                    s->disconnectFromHost();
                });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    int count(const QByteArray &method, const QString &pathPrefix) const {
        int n = 0;
        for (const HttpRequest &r : requests)
            if (r.method == method && r.path().startsWith(pathPrefix))
                ++n;
        return n;
    }
};
