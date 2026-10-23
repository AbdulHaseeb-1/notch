#pragma once
// Test doubles for the Buddy agent tests: a fake Anthropic / OpenAI HTTP server
// that replays scripted replies and records requests, a music backend that
// records its calls, and the builders the scripts use for replies and tool calls.
#include <QtTest>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QPointer>
#include <QSharedPointer>
#include <QTimer>

#include "BuddyProvider.h"
#include "ChatSessions.h"
#include "SessionLink.h"
#include "ComputerUse.h"
#include "Connector.h"

// FakeAnthropic is a QObject, so it must stay out of the anonymous namespace: moc puts its
// vtable in its own translation unit, and a class in an unnamed namespace gets a different
// one in each unit, which leaves the test's reference to the vtable unresolved at link time.
struct Reply {
    int status;
    QByteArray body;
    QList<QByteArray> sse = {};   // when set: text/event-stream, written chunk by chunk
    int gapMs = 0;
    int declaredLength = -1;      // SSE only: send a Content-Length larger than the body (dropped mid-stream)
};

class FakeAnthropic : public QObject
{
    Q_OBJECT
public:
    QList<Reply> queue;
    QList<QJsonObject> requests;
    QList<QByteArray> headers;   // raw HTTP header block of each request, lower-cased

    bool start() {
        connect(&server, &QTcpServer::newConnection, this, &FakeAnthropic::onConnection);
        return server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

private:
    QTcpServer server;
    QHash<QTcpSocket *, QByteArray> buffers;

    void onConnection() {
        while (QTcpSocket *s = server.nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }
    void onData(QTcpSocket *s) {
        QByteArray &buf = buffers[s];
        buf += s->readAll();
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        int len = 0;
        for (const QByteArray &line : buf.left(headerEnd).split('\n'))
            if (line.toLower().startsWith("content-length:"))
                len = line.mid(15).trimmed().toInt();
        if (buf.size() < headerEnd + 4 + len)
            return;
        headers.append(buf.left(headerEnd).toLower());
        requests.append(QJsonDocument::fromJson(buf.mid(headerEnd + 4, len)).object());
        buffers.remove(s);
        const Reply r = queue.isEmpty() ? Reply{ 500, "{\"error\":{\"message\":\"no reply queued\"}}" }
                                        : queue.takeFirst();
        if (!r.sse.isEmpty()) {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
                     + (r.declaredLength > 0 ? "Content-Length: " + QByteArray::number(r.declaredLength) + "\r\n" : QByteArray())
                     + "Connection: close\r\n\r\n");
            QPointer<QTcpSocket> sp(s);
            const QList<QByteArray> chunks = r.sse;
            const auto sent = QSharedPointer<int>::create(0);
            for (int i = 0; i < chunks.size(); ++i)
                QTimer::singleShot(r.gapMs * (i + 1), this, [sp, chunks, sent] {
                    // Each timer sends the next chunk in line, not the one it was started with:
                    // timers due at the same moment do not always fire in the order they were
                    // started, and bytes out of order broke the event stream.
                    if (!sp)
                        return;
                    sp->write(chunks.at((*sent)++));
                    sp->flush();
                    if (*sent == chunks.size())
                        sp->disconnectFromHost();
                });
            return;
        }
        QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + (r.status == 200 ? " OK" : " ERR")
            + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(r.body.size())
            + (r.status == 529 ? "\r\nretry-after: 1" : "")
            + "\r\nConnection: close\r\n\r\n" + r.body;
        s->write(out);
        s->disconnectFromHost();
    }
};

namespace {

QByteArray toolUse(const QString &id, const QString &command)
{
    QJsonObject msg{
        { "id", "msg_" + id }, { "type", "message" }, { "role", "assistant" },
        { "content", QJsonArray{ QJsonObject{ { "type", "tool_use" }, { "id", id }, { "name", "bash" },
                                              { "input", QJsonObject{ { "command", command } } } } } },
        { "stop_reason", "tool_use" },
        { "usage", QJsonObject{ { "input_tokens", 100 }, { "output_tokens", 10 }, { "cache_read_input_tokens", 80 } } },
    };
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

// One assistant reply holding several tool_use blocks (a batch of actions).
// A block with a toolset_name is a computer toolset member call.
struct Call { QString id; QString name; QJsonObject input; QString toolset = {}; };

QByteArray toolUseBatch(const QList<Call> &calls)
{
    QJsonArray content;
    for (const Call &c : calls) {
        QJsonObject b{ { "type", "tool_use" }, { "id", c.id }, { "name", c.name }, { "input", c.input } };
        if (!c.toolset.isEmpty())
            b["toolset_name"] = c.toolset;
        content.append(b);
    }
    QJsonObject msg{
        { "id", "msg_batch" }, { "type", "message" }, { "role", "assistant" }, { "content", content },
        { "stop_reason", "tool_use" }, { "usage", QJsonObject{ { "input_tokens", 10 }, { "output_tokens", 5 } } },
    };
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

Call member(const QString &id, const QString &name, const QJsonObject &input = {})
{
    return Call{ id, name, input, QStringLiteral("computer") };
}

QByteArray apiError(const QString &message)
{
    return QJsonDocument(QJsonObject{ { "type", "error" },
        { "error", QJsonObject{ { "type", "invalid_request_error" }, { "message", message } } } }).toJson(QJsonDocument::Compact);
}

QJsonObject computerTool(const QJsonObject &req)
{
    for (const QJsonValue &t : req.value("tools").toArray()) {
        const QString type = t.toObject().value("type").toString();
        if (type.startsWith("computer_") || t.toObject().value("name").toString() == "computer")
            return t.toObject();
    }
    return {};
}

QByteArray toolUseNamed(const QString &id, const QString &name, const QJsonObject &input)
{
    QJsonObject msg{
        { "id", "msg_" + id }, { "type", "message" }, { "role", "assistant" },
        { "content", QJsonArray{ QJsonObject{ { "type", "tool_use" }, { "id", id }, { "name", name }, { "input", input } } } },
        { "stop_reason", "tool_use" }, { "usage", QJsonObject{ { "input_tokens", 10 }, { "output_tokens", 5 } } },
    };
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

QStringList toolNames(const QJsonObject &req)
{
    QStringList out;
    for (const QJsonValue &t : req.value("tools").toArray())
        out << t.toObject().value("name").toString();
    return out;
}

// The conversation with the per-request cache markers taken off, to compare
// what the model sees from one request to the next.
QJsonArray withoutCacheMarkers(const QJsonArray &messages)
{
    QJsonArray out;
    for (const QJsonValue &mv : messages) {
        QJsonObject m = mv.toObject();
        if (m.value("content").isArray()) {
            QJsonArray blocks;
            for (const QJsonValue &bv : m.value("content").toArray()) {
                QJsonObject b = bv.toObject();
                b.remove("cache_control");
                blocks.append(b);
            }
            // A string turned into a one-block array for its marker is the same message.
            if (blocks.size() == 1 && blocks.first().toObject().value("type").toString() == "text"
                    && blocks.first().toObject().size() == 2)
                m["content"] = blocks.first().toObject().value("text").toString();
            else
                m["content"] = blocks;
        }
        out.append(m);
    }
    return out;
}

// The text of a message whether it was sent as a string or, to carry a cache
// marker, as one text block.
QString messageText(const QJsonObject &msg)
{
    const QJsonValue c = msg.value("content");
    if (c.isString())
        return c.toString();
    QString out;
    for (const QJsonValue &b : c.toArray())
        out += b.toObject().value("text").toString();
    return out;
}

QString systemText(const QJsonObject &req)
{
    return req.value("system").toArray().first().toObject().value("text").toString();
}

QByteArray text(const QString &t)
{
    QJsonObject msg{
        { "id", "msg_t" }, { "type", "message" }, { "role", "assistant" },
        { "content", QJsonArray{ QJsonObject{ { "type", "text" }, { "text", t } } } },
        { "stop_reason", "end_turn" },
        { "usage", QJsonObject{ { "input_tokens", 50 }, { "output_tokens", 5 } } },
    };
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

// The tool_result blocks the client sent in its latest request.
QJsonArray lastToolResults(const QList<QJsonObject> &reqs)
{
    const QJsonArray msgs = reqs.constLast().value("messages").toArray();
    const QJsonObject last = msgs.last().toObject();
    QJsonArray out;
    for (const QJsonValue &b : last.value("content").toArray())
        if (b.toObject().value("type").toString() == "tool_result")
            out.append(b);
    return out;
}

QByteArray openAiToolCall(const QString &id, const QString &command)
{
    QJsonObject fn{ { "name", "bash" },
                    { "arguments", QString::fromUtf8(QJsonDocument(QJsonObject{ { "command", command } }).toJson(QJsonDocument::Compact)) } };
    QJsonObject msg{ { "role", "assistant" }, { "content", QJsonValue::Null },
                     { "tool_calls", QJsonArray{ QJsonObject{ { "id", id }, { "type", "function" }, { "function", fn } } } } };
    QJsonObject r{ { "id", "chatcmpl-1" }, { "object", "chat.completion" },
                   { "choices", QJsonArray{ QJsonObject{ { "index", 0 }, { "message", msg }, { "finish_reason", "tool_calls" } } } },
                   { "usage", QJsonObject{ { "prompt_tokens", 100 }, { "completion_tokens", 10 },
                                           { "prompt_tokens_details", QJsonObject{ { "cached_tokens", 64 } } } } } };
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

QByteArray openAiText(const QString &t)
{
    QJsonObject r{ { "id", "chatcmpl-2" }, { "object", "chat.completion" },
                   { "choices", QJsonArray{ QJsonObject{ { "index", 0 },
                        { "message", QJsonObject{ { "role", "assistant" }, { "content", t } } }, { "finish_reason", "stop" } } } },
                   { "usage", QJsonObject{ { "prompt_tokens", 50 }, { "completion_tokens", 5 } } } };
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

QByteArray sseEvent(const char *event, const QJsonObject &data)
{
    return QByteArray("event: ") + event + "\ndata: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
}

// Anthropic SSE for a text answer, split into one chunk per word.
QList<QByteArray> sseText(const QStringList &words)
{
    QList<QByteArray> out;
    out << sseEvent("message_start", { { "type", "message_start" }, { "message", QJsonObject{
              { "id", "msg_s" }, { "type", "message" }, { "role", "assistant" }, { "model", "claude-stream" },
              { "content", QJsonArray() }, { "usage", QJsonObject{ { "input_tokens", 20 }, { "cache_read_input_tokens", 11 } } } } } })
          + sseEvent("content_block_start", { { "type", "content_block_start" }, { "index", 0 },
                                              { "content_block", QJsonObject{ { "type", "text" }, { "text", "" } } } });
    for (const QString &w : words)
        out << sseEvent("content_block_delta", { { "type", "content_block_delta" }, { "index", 0 },
                                                 { "delta", QJsonObject{ { "type", "text_delta" }, { "text", w } } } });
    out << sseEvent("content_block_stop", { { "type", "content_block_stop" }, { "index", 0 } })
           + sseEvent("message_delta", { { "type", "message_delta" }, { "delta", QJsonObject{ { "stop_reason", "end_turn" } } },
                                         { "usage", QJsonObject{ { "output_tokens", 6 } } } })
           + sseEvent("message_stop", { { "type", "message_stop" } });
    return out;
}

// Anthropic SSE for a bash tool_use with the input JSON split in two.
QList<QByteArray> sseToolUse(const QString &id, const QString &command)
{
    const QByteArray json = QJsonDocument(QJsonObject{ { "command", command } }).toJson(QJsonDocument::Compact);
    const int half = json.size() / 2;
    QList<QByteArray> out;
    out << sseEvent("message_start", { { "type", "message_start" }, { "message", QJsonObject{
              { "id", "msg_t" }, { "type", "message" }, { "role", "assistant" }, { "content", QJsonArray() },
              { "usage", QJsonObject{ { "input_tokens", 20 } } } } } })
        << sseEvent("content_block_start", { { "type", "content_block_start" }, { "index", 0 },
              { "content_block", QJsonObject{ { "type", "tool_use" }, { "id", id }, { "name", "bash" }, { "input", QJsonObject() } } } })
        << sseEvent("content_block_delta", { { "type", "content_block_delta" }, { "index", 0 },
              { "delta", QJsonObject{ { "type", "input_json_delta" }, { "partial_json", QString::fromUtf8(json.left(half)) } } } })
        << sseEvent("content_block_delta", { { "type", "content_block_delta" }, { "index", 0 },
              { "delta", QJsonObject{ { "type", "input_json_delta" }, { "partial_json", QString::fromUtf8(json.mid(half)) } } } })
           + sseEvent("content_block_stop", { { "type", "content_block_stop" }, { "index", 0 } })
           + sseEvent("message_delta", { { "type", "message_delta" }, { "delta", QJsonObject{ { "stop_reason", "tool_use" } } },
                                         { "usage", QJsonObject{ { "output_tokens", 9 } } } })
           + sseEvent("message_stop", { { "type", "message_stop" } });
    return out;
}

// Anthropic SSE for a long text answer that ends with `stopReason`
// (max_tokens = cut by the output limit). The whole stream is re-split into
// awkward network chunks: mid-line, mid-"data:" prefix and inside multi-byte
// UTF-8 sequences, with \r\n line ends on some events.
QList<QByteArray> sseLong(const QStringList &pieces, const QString &stopReason, int chunk = 13)
{
    QByteArray all = sseEvent("message_start", { { "type", "message_start" }, { "message", QJsonObject{
              { "id", "msg_l" }, { "type", "message" }, { "role", "assistant" }, { "model", "claude-long" },
              { "content", QJsonArray() }, { "usage", QJsonObject{ { "input_tokens", 9000 } } } } } })
        + sseEvent("content_block_start", { { "type", "content_block_start" }, { "index", 0 },
                                            { "content_block", QJsonObject{ { "type", "text" }, { "text", "" } } } });
    for (int i = 0; i < pieces.size(); ++i) {
        QByteArray ev = sseEvent("content_block_delta", { { "type", "content_block_delta" }, { "index", 0 },
                                                          { "delta", QJsonObject{ { "type", "text_delta" }, { "text", pieces.at(i) } } } });
        if (i % 3 == 1)
            ev.replace("\n", "\r\n");
        all += ev;
        if (i % 5 == 0)
            all += ": ping\n\n";
    }
    all += sseEvent("content_block_stop", { { "type", "content_block_stop" }, { "index", 0 } })
         + sseEvent("message_delta", { { "type", "message_delta" }, { "delta", QJsonObject{ { "stop_reason", stopReason } } },
                                       { "usage", QJsonObject{ { "output_tokens", 2048 } } } })
         + sseEvent("message_stop", { { "type", "message_stop" } });
    QList<QByteArray> out;
    for (int i = 0; i < all.size(); i += chunk)
        out << all.mid(i, chunk);
    return out;
}

// A long multi-turn session, as Buddy.qml sends it.
QVariantList longSession(int turns, int answerChars)
{
    QVariantList ctx;
    for (int i = 0; i < turns; ++i) {
        ctx << QVariantMap{ { "role", "user" }, { "content", QStringLiteral("question %1").arg(i) } };
        ctx << QVariantMap{ { "role", "assistant" }, { "content", QStringLiteral("answer %1 ").arg(i) + QString(answerChars, 'a') } };
    }
    return ctx;
}

QStringList longPieces(const QString &tag, int n)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out << QStringLiteral("%1-%2 naïve café — 日本語 ✓ ").arg(tag).arg(i);
    return out;
}

} // namespace

// A music pill that records what it was asked to do.
class FakeMusic final : public MusicBackend
{
public:
    MusicStatus st;
    QStringList calls;
    QStringList queue;
    QString folder = QStringLiteral("C:/Users/Test/Music");
    bool canPlay = true;
    int addResult = 3;
    QString addError;
    QString addedPath;

    FakeMusic() { st.ready = true; st.volumePct = 80; st.source = QStringLiteral("Noch Player"); }
    MusicStatus status() const override { MusicStatus s = st; s.queueCount = queue.size(); return s; }
    bool play(QString *why) override {
        calls << QStringLiteral("play");
        if (!canPlay) {
            if (why)
                *why = QStringLiteral("Nothing is queued in Noch's player and no other app is playing.");
            return false;
        }
        st.playing = true;
        st.hasTrack = true;
        if (st.title.isEmpty()) {
            st.title = QStringLiteral("Blue Train");
            st.artist = QStringLiteral("John Coltrane");
        }
        return true;
    }
    void pause() override { calls << QStringLiteral("pause"); st.playing = false; }
    void next() override { calls << QStringLiteral("next"); st.title = QStringLiteral("Moments Notice"); }
    void previous() override { calls << QStringLiteral("previous"); }
    void setVolume(int pct) override { calls << QStringLiteral("volume %1").arg(pct); st.volumePct = pct; }
    void setMuted(bool m) override { calls << (m ? QStringLiteral("mute") : QStringLiteral("unmute")); st.muted = m; }
    QStringList queueTitles(int limit) const override { return queue.mid(0, limit); }
    int playMatching(const QString &q, QString *played) override {
        calls << QStringLiteral("play_track %1").arg(q);
        for (const QString &t : queue)
            if (t.toLower().contains(q.toLower())) {
                st.playing = true; st.hasTrack = true; st.title = t;
                if (played) *played = t;
                return 1;
            }
        return 0;
    }
    QString musicFolder() const override { return folder; }
    void add(const QString &path, std::function<void(int, const QString &)> done) override {
        calls << QStringLiteral("add");
        addedPath = path;
        done(addResult, addError);
    }
};

// A Mem0 server (Platform API shape) that answers every search with `memories`
// and every add with success, and records the requests it saw.
struct FakeMem0 {
    QTcpServer server;
    QStringList memories;
    int addStatus = 200;
    QList<QByteArray> searches;   // request bodies
    QList<QByteArray> adds;

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buf = QSharedPointer<QByteArray>::create();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    *buf += s->readAll();
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const int len = QString::fromLatin1(buf->left(end)).section("Content-Length:", 1)
                                        .section("\r\n", 0, 0).trimmed().toInt();
                    if (buf->size() < end + 4 + len)
                        return;
                    const bool isAdd = buf->startsWith("POST /v3/memories/add/");
                    const QByteArray body = buf->mid(end + 4, len);
                    QByteArray reply;
                    int status = 200;
                    if (isAdd) {
                        adds.append(body);
                        status = addStatus;
                        reply = "{\"status\":\"SUCCEEDED\",\"results\":[]}";
                    } else {
                        searches.append(body);
                        QJsonArray results;
                        for (const QString &m : std::as_const(memories))
                            results.append(QJsonObject{ { "id", "m1" }, { "memory", m }, { "score", 0.9 } });
                        reply = QJsonDocument(QJsonObject{ { "results", results } }).toJson(QJsonDocument::Compact);
                    }
                    buf->clear();
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                             + QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply);
                    s->disconnectFromHost();
                });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
};

// A connected app (the Gmail / MCP shape) that records what it is asked and answers from a script.
// It lives outside the anonymous namespace for the same reason as FakeAnthropic (moc).
class FakeConnector : public Connector
{
    Q_OBJECT
public:
    QList<connectors::ToolDef> defs;
    QStringList calls;               // tool names, in order
    QList<QJsonObject> callArgs;
    QString reply = QStringLiteral("2 messages: Invoice from Alice; Lunch?");
    bool replyOk = true;
    int replyDelayMs = 0;

    FakeConnector() {
        connectors::ToolDef read;
        read.name = QStringLiteral("fake_search");
        read.description = QStringLiteral("Search the fake mailbox.");
        read.inputSchema = QJsonObject{ { "type", "object" },
            { "properties", QJsonObject{ { "query", QJsonObject{ { "type", "string" } } },
                                         { "max_results", QJsonObject{ { "type", "integer" } } } } },
            { "required", QJsonArray{ "query" } } };
        read.access = connectors::Access::Read;
        connectors::ToolDef write;
        write.name = QStringLiteral("fake_send");
        write.description = QStringLiteral("Send a fake email.");
        write.inputSchema = QJsonObject{ { "type", "object" },
            { "properties", QJsonObject{ { "to", QJsonObject{ { "type", "string" } } },
                                         { "body", QJsonObject{ { "type", "string" } } } } },
            { "required", QJsonArray{ "to", "body" } } };
        write.access = connectors::Access::Write;
        defs = { read, write };
    }
    void makeConnected() { setState(connectors::State::Connected, QStringLiteral("Connected as me@example.com"), QStringLiteral("me@example.com")); }
    void makeDisconnected() { setState(connectors::State::Disconnected, QStringLiteral("Not connected")); }

    QString id() const override { return QStringLiteral("fake"); }
    QString name() const override { return QStringLiteral("FakeMail"); }
    QString summary() const override { return QStringLiteral("A fake app."); }
    QString kind() const override { return QStringLiteral("oauth"); }
    QList<connectors::Permission> permissions() const override { return {}; }
    QList<connectors::ToolDef> tools() const override { return connected() ? defs : QList<connectors::ToolDef>(); }
    void connectAccount() override { makeConnected(); }
    void disconnectAccount() override { makeDisconnected(); }
    void call(const QString &tool, const QJsonObject &args, connectors::Done done) override {
        calls << tool;
        callArgs << args;
        const bool ok = replyOk;
        const QString text = reply;
        QTimer::singleShot(replyDelayMs, this, [done, ok, text] { done(ok, text); });
    }
};
