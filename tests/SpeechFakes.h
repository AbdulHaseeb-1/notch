#pragma once
// Test doubles shared by the voice tests: a microphone fed by the test, a
// speech-to-text server that records requests, and synthetic audio.
#include <QAudioFormat>
#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtMath>

#include "VoiceAudio.h"
#include "VoiceInput.h"

namespace {

// A sine wave as 16-bit mono PCM at the speech rate.
QByteArray tone(double seconds, double hz = 300, double amp = 0.3, int rate = voice::kSampleRate)
{
    const int n = int(seconds * rate);
    QByteArray out(n * 2, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(out.data());
    for (int i = 0; i < n; ++i)
        s[i] = qint16(amp * 32767.0 * std::sin(2 * M_PI * hz * i / rate));
    return out;
}

QByteArray silence(double seconds)
{
    return QByteArray(int(seconds * voice::kSampleRate) * 2, '\0');
}

// A microphone that plays back what a test feeds it.
class FakeCapture : public AudioCapture
{
public:
    bool startOk = true;
    QString startError = QStringLiteral("No microphone was found.");
    int starts = 0, stops = 0;
    bool open = false;
    bool start(QString *error) override { ++starts; if (!startOk) { *error = startError; return false; } open = true; return true; }
    void stop() override { if (open) ++stops; open = false; }
    void feed(const QByteArray &pcm) { emit audio(pcm); }
    void breakDown(const QString &why) { emit failed(why); }
};

// A speech-to-text server that records the request and answers as told.
class FakeStt : public QObject
{
public:
    explicit FakeStt(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&server, &QTcpServer::newConnection, this, &FakeStt::onConnection);
        server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    int status = 200;
    QByteArray reply = "{\"text\":\"hello world\"}";
    QList<QByteArray> replies;   // answers used first, in order, before `reply`
    int delayMs = 0;
    int requests = 0;
    QByteArray requestLine, headers, body;

private:
    QTcpServer server;
    QHash<QTcpSocket *, QByteArray> buffers;
    void onConnection()
    {
        while (QTcpSocket *s = server.nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }
    void onData(QTcpSocket *s)
    {
        QByteArray &buf = buffers[s];
        buf += s->readAll();
        const int he = buf.indexOf("\r\n\r\n");
        if (he < 0)
            return;
        int len = 0;
        for (const QByteArray &line : buf.left(he).split('\n'))
            if (line.toLower().startsWith("content-length:"))
                len = line.mid(15).trimmed().toInt();
        if (buf.size() < he + 4 + len)
            return;
        ++requests;
        const QByteArray answer = replies.isEmpty() ? reply : replies.takeFirst();
        requestLine = buf.left(buf.indexOf("\r\n"));
        headers = buf.left(he).toLower();
        body = buf.mid(he + 4, len);
        buffers.remove(s);
        QPointer<QTcpSocket> sp(s);
        const QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer;
        QTimer::singleShot(delayMs, this, [sp, out] { if (sp) { sp->write(out); sp->disconnectFromHost(); } });
    }
};

} // namespace
