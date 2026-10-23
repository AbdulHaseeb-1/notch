#include "McpConnector.h"

#include "SecretStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

#ifndef NOCH_VERSION
#define NOCH_VERSION "0"
#endif

using connectors::Access;
using connectors::Done;
using connectors::ToolDef;

namespace mcp {

QJsonObject Spec::toJson() const
{
    return QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("name"), name },
                        { QStringLiteral("transport"), transport }, { QStringLiteral("target"), target },
                        { QStringLiteral("autoConnect"), autoConnect } };
}

Spec Spec::fromJson(const QJsonObject &o)
{
    Spec s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.transport = o.value(QStringLiteral("transport")).toString();
    s.target = o.value(QStringLiteral("target")).toString();
    s.autoConnect = o.value(QStringLiteral("autoConnect")).toBool(true);
    return s;
}

QList<QPair<QString, QString>> parseHeaderLines(const QString &text)
{
    QList<QPair<QString, QString>> out;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString name = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        static const QRegularExpression token(QStringLiteral("^[A-Za-z0-9-]+$"));
        // No line breaks in a value: a header cannot be used to smuggle a second one.
        if (!token.match(name).hasMatch() || value.contains(QLatin1Char('\r')))
            continue;
        out.append({ name, value });
    }
    return out;
}

QList<QPair<QString, QString>> parseEnvLines(const QString &text)
{
    QList<QPair<QString, QString>> out;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString name = line.left(eq).trimmed();
        static const QRegularExpression ident(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (!ident.match(name).hasMatch())
            continue;
        out.append({ name, line.mid(eq + 1).trimmed() });
    }
    return out;
}

QStringList splitCommand(const QString &command)
{
    return QProcess::splitCommand(command.trimmed());
}

QString validateSpec(const QString &transport, const QString &target)
{
    const QString t = target.trimmed();
    if (t.isEmpty())
        return QObject::tr("Enter the server's address or command.");
    if (transport == QLatin1String("http")) {
        const QUrl url(t);
        if (!url.isValid() || url.host().isEmpty())
            return QObject::tr("That is not a valid address.");
        if (url.scheme() != QLatin1String("https") && !(url.scheme() == QLatin1String("http") && connectors::isLoopbackHost(url.host())))
            return QObject::tr("Use an https:// address (plain http is only allowed for this computer).");
        return QString();
    }
    if (transport == QLatin1String("stdio")) {
        if (splitCommand(t).isEmpty())
            return QObject::tr("That is not a valid command line.");
        return QString();
    }
    return QObject::tr("Pick how Noch reaches the server.");
}

QString exposedName(const QString &serverSlug, const QString &toolName, const QStringList &taken)
{
    static const QRegularExpression bad(QStringLiteral("[^A-Za-z0-9_-]"));
    QString tool = toolName;
    tool.replace(bad, QStringLiteral("_"));
    const QString prefix = QStringLiteral("mcp_%1_").arg(serverSlug);
    QString name = (prefix + tool).left(64);
    int n = 2;
    while (taken.contains(name)) {
        const QString suffix = QStringLiteral("_%1").arg(n++);
        name = (prefix + tool).left(64 - suffix.size()) + suffix;
    }
    return name;
}

QString resultText(const QJsonObject &result, bool *isError)
{
    if (isError)
        *isError = result.value(QStringLiteral("isError")).toBool();
    QStringList parts;
    for (const QJsonValue &cv : result.value(QStringLiteral("content")).toArray()) {
        const QJsonObject c = cv.toObject();
        const QString type = c.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            parts << c.value(QStringLiteral("text")).toString();
        } else if (type == QLatin1String("resource")) {
            const QJsonObject r = c.value(QStringLiteral("resource")).toObject();
            parts << (r.contains(QStringLiteral("text")) ? r.value(QStringLiteral("text")).toString()
                                                         : QObject::tr("[resource %1]").arg(r.value(QStringLiteral("uri")).toString()));
        } else if (type == QLatin1String("resource_link")) {
            parts << QObject::tr("[link %1]").arg(c.value(QStringLiteral("uri")).toString());
        } else {
            parts << QObject::tr("[%1 content not shown]").arg(type.isEmpty() ? QStringLiteral("binary") : type);
        }
    }
    if (parts.isEmpty() && result.contains(QStringLiteral("structuredContent")))
        parts << QString::fromUtf8(QJsonDocument(result.value(QStringLiteral("structuredContent")).toObject()).toJson(QJsonDocument::Compact));
    return parts.isEmpty() ? QString() : parts.join(QLatin1Char('\n'));
}

QString sseData(const QByteArray &block)
{
    QStringList data;
    for (const QByteArray &raw : block.split('\n')) {
        QByteArray line = raw;
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.startsWith("data:")) {
            QByteArray v = line.mid(5);
            if (v.startsWith(' '))
                v.remove(0, 1);
            data << QString::fromUtf8(v);
        }
    }
    return data.join(QLatin1Char('\n'));
}

} // namespace mcp

// ---------------------------------------------------------------------------

McpConnector::McpConnector(const mcp::Spec &spec, SecretStore *secrets, QObject *parent)
    : Connector(parent)
    , m_spec(spec)
    , m_secrets(secrets)
{
    setState(connectors::State::Disconnected, tr("Not connected"));
}

McpConnector::~McpConnector()
{
    m_stopping = true;
    stopTransport();
}

QString McpConnector::summary() const
{
    return m_spec.transport == QLatin1String("http")
        ? tr("MCP server at %1").arg(QUrl(m_spec.target).host())
        : tr("MCP server run as: %1").arg(m_spec.target.left(60));
}

QList<connectors::Permission> McpConnector::permissions() const
{
    int reads = 0, writes = 0;
    for (const ToolDef &t : m_tools)
        (t.access == Access::Read ? reads : writes)++;
    if (m_tools.isEmpty() || !connected())
        return { { tr("Whatever tools the server offers once connected. Tools that can change things ask you every time."), Access::Write } };
    QList<connectors::Permission> out;
    if (reads > 0)
        out.append({ tr("%1 read-only tool(s) the server marks as safe to run").arg(reads), Access::Read });
    if (writes > 0)
        out.append({ tr("%1 tool(s) that may change things: you are asked every time").arg(writes), Access::Write });
    return out;
}

// ---- transport ---------------------------------------------------------------------

void McpConnector::connectAccount()
{
    if (m_spec.transport != QLatin1String("http") && m_spec.transport != QLatin1String("stdio")) {
        fail(tr("Unknown transport."));
        return;
    }
    const QString bad = mcp::validateSpec(m_spec.transport, m_spec.target);
    if (!bad.isEmpty()) {
        fail(bad);
        return;
    }
    stopTransport();
    ++m_epoch;
    m_spec.autoConnect = true;
    m_tools.clear();
    m_original.clear();
    m_sessionId.clear();
    m_protocol.clear();
    setState(connectors::State::Connecting, tr("Connecting…"));
    startTransport();
    if (state() == connectors::State::Error)
        return;   // the program could not even start; startTransport() already said why
    handshake();
}

void McpConnector::disconnectAccount()
{
    ++m_epoch;
    m_spec.autoConnect = false;
    failAll(tr("Disconnected."));
    stopTransport();
    m_tools.clear();
    m_original.clear();
    setState(connectors::State::Disconnected, tr("Not connected"));
}

void McpConnector::cancelAll()
{
    failAll(tr("Cancelled."));
}

void McpConnector::fail(const QString &status)
{
    ++m_epoch;
    failAll(status);
    stopTransport();
    m_tools.clear();
    m_original.clear();
    setState(connectors::State::Error, status);
}

void McpConnector::startTransport()
{
    if (m_spec.transport != QLatin1String("stdio"))
        return;
    QStringList argv = mcp::splitCommand(m_spec.target);
    QString program = argv.takeFirst();
    // npx, npm, uvx ... are .cmd/.exe shims on Windows: resolve them the way a shell would.
    if (!program.contains(QLatin1Char('/')) && !program.contains(QLatin1Char('\\'))) {
        const QString found = QStandardPaths::findExecutable(program);
        if (!found.isEmpty())
            program = found;
    }
    auto *proc = new QProcess(this);
    m_proc = proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_secrets)
        for (const auto &kv : mcp::parseEnvLines(m_secrets->get(secretsKey())))
            env.insert(kv.first, kv.second);
    proc->setProcessEnvironment(env);
    m_inbuf.clear();
    m_stderrTail.clear();
    connect(proc, &QProcess::readyReadStandardOutput, this, &McpConnector::onStdioData);
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        m_stderrTail = (m_stderrTail + proc->readAllStandardError()).right(2000);
    });
    const quint64 epoch = m_epoch;
    connect(proc, &QProcess::errorOccurred, this, [this, epoch, proc](QProcess::ProcessError e) {
        if (m_stopping || epoch != m_epoch || e != QProcess::FailedToStart)
            return;
        fail(tr("Could not start the server (%1).").arg(proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, epoch](int code, QProcess::ExitStatus) {
        if (m_stopping || epoch != m_epoch)
            return;
        QString tail = QString::fromUtf8(m_stderrTail).simplified();
        if (tail.size() > 160)
            tail = QStringLiteral("…") + tail.right(159);
        fail(tail.isEmpty() ? tr("The server stopped (exit code %1).").arg(code)
                            : tr("The server stopped (exit code %1): %2").arg(code).arg(tail));
    });
    proc->start(program, argv);
}

void McpConnector::stopTransport()
{
    if (!m_proc)
        return;
    QProcess *p = m_proc.data();
    m_proc = nullptr;
    const bool was = m_stopping;
    m_stopping = true;
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->closeWriteChannel();
        if (!p->waitForFinished(300)) {
            p->terminate();
            if (!p->waitForFinished(500))
                p->kill();
        }
    }
    p->deleteLater();
    m_stopping = was;
}

void McpConnector::onStdioData()
{
    if (!m_proc)
        return;
    m_inbuf += m_proc->readAllStandardOutput();
    if (m_inbuf.size() > (8 << 20)) {   // a server that never ends a line
        fail(tr("The server sent an oversized message."));
        return;
    }
    qsizetype nl;
    while ((nl = m_inbuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_inbuf.left(nl).trimmed();
        m_inbuf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            onMessage(doc.object());
        else if (doc.isArray())
            for (const QJsonValue &v : doc.array())
                onMessage(v.toObject());
        // Anything else is a log line the server wrote to stdout: ignored.
    }
}

void McpConnector::send(const QJsonObject &message, qint64 id)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (m_spec.transport == QLatin1String("stdio")) {
        if (m_proc && m_proc->state() == QProcess::Running)
            m_proc->write(payload + "\n");
        else if (id > 0)
            finishPending(id, false, QJsonValue(), tr("The server is not running."));
        return;
    }
    sendHttp(payload, id);
}

void McpConnector::sendHttp(const QByteArray &payload, qint64 id)
{
    QNetworkRequest req{ QUrl(m_spec.target) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json, text/event-stream");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(callTimeoutMs + 5000);
    if (!m_sessionId.isEmpty())
        req.setRawHeader("Mcp-Session-Id", m_sessionId.toUtf8());
    if (!m_protocol.isEmpty())
        req.setRawHeader("MCP-Protocol-Version", m_protocol.toUtf8());
    // The user's headers (a token) go only over https, or to this computer.
    const QUrl url = req.url();
    if (m_secrets && (url.scheme() == QLatin1String("https") || connectors::isLoopbackHost(url.host())))
        for (const auto &kv : mcp::parseHeaderLines(m_secrets->get(secretsKey())))
            req.setRawHeader(kv.first.toUtf8(), kv.second.toUtf8());

    QNetworkReply *reply = m_nam.post(req, payload);
    const quint64 epoch = m_epoch;
    auto sse = std::make_shared<QByteArray>();
    auto isStream = std::make_shared<bool>(false);
    auto sessionRead = std::make_shared<bool>(false);
    auto takeSession = [this, reply, sessionRead] {
        if (*sessionRead)
            return;
        const QByteArray sid = reply->rawHeader("Mcp-Session-Id");
        if (!sid.isEmpty()) {
            m_sessionId = QString::fromLatin1(sid);
            *sessionRead = true;
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, sse, isStream, epoch, takeSession] {
        takeSession();
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        if (!ct.contains(QLatin1String("text/event-stream")))
            return;   // plain JSON is read once the reply is complete
        *isStream = true;
        *sse += reply->readAll();
        *sse = sse->replace("\r\n", "\n");
        qsizetype end;
        while ((end = sse->indexOf("\n\n")) >= 0) {
            const QString data = mcp::sseData(sse->left(end));
            sse->remove(0, end + 2);
            if (epoch != m_epoch)
                return;
            const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
            if (doc.isObject())
                onMessage(doc.object());
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, epoch, isStream, takeSession] {
        takeSession();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString transport = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (epoch != m_epoch)
            return;
        if (status >= 200 && status < 300 && !*isStream && !body.trimmed().isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject())
                onMessage(doc.object());
            else if (doc.isArray())
                for (const QJsonValue &v : doc.array())
                    onMessage(v.toObject());
        }
        // The request is still unanswered: say why.
        if (id > 0 && m_pending.contains(id)) {
            QString why;
            if (status == 401 || status == 403)
                why = tr("The server refused the credentials (HTTP %1). Check the token: remove the server and add it again.").arg(status);
            else if (status >= 400)
                why = tr("The server answered HTTP %1.").arg(status);
            else if (!transport.isEmpty())
                why = transport.mid(transport.lastIndexOf(QLatin1String(": ")) >= 0 ? transport.lastIndexOf(QLatin1String(": ")) + 2 : 0);
            else
                why = tr("The server sent no answer.");
            finishPending(id, false, QJsonValue(), why);
        }
    });
}

// ---- JSON-RPC ----------------------------------------------------------------------

void McpConnector::rpc(const QString &method, const QJsonObject &params, int timeoutMs, RpcDone done)
{
    const qint64 id = m_nextId++;
    Pending p;
    p.done = std::move(done);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    connect(p.timer, &QTimer::timeout, this, [this, id, method] {
        finishPending(id, false, QJsonValue(), tr("Timed out waiting for %1.").arg(method));
    });
    p.timer->start(timeoutMs);
    m_pending.insert(id, p);
    QJsonObject msg{ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), double(id) },
                     { QStringLiteral("method"), method } };
    if (!params.isEmpty())
        msg.insert(QStringLiteral("params"), params);
    send(msg, id);
}

void McpConnector::notify(const QString &method)
{
    send(QJsonObject{ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("method"), method } }, 0);
}

void McpConnector::finishPending(qint64 id, bool ok, const QJsonValue &result, const QString &error)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Pending p = it.value();
    m_pending.erase(it);
    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
    }
    if (p.done)
        p.done(ok, result, error);
}

void McpConnector::failAll(const QString &error)
{
    const QList<qint64> ids = m_pending.keys();
    for (qint64 id : ids)
        finishPending(id, false, QJsonValue(), error);
}

void McpConnector::onMessage(const QJsonObject &msg)
{
    const bool hasMethod = msg.contains(QStringLiteral("method"));
    if (hasMethod) {
        // A request from the server. We offer no client features, so: ping is answered,
        // anything else is "method not found".
        if (msg.contains(QStringLiteral("id")) && m_spec.transport == QLatin1String("stdio")) {
            const bool ping = msg.value(QStringLiteral("method")).toString() == QLatin1String("ping");
            QJsonObject reply{ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), msg.value(QStringLiteral("id")) } };
            if (ping)
                reply.insert(QStringLiteral("result"), QJsonObject());
            else
                reply.insert(QStringLiteral("error"), QJsonObject{ { QStringLiteral("code"), -32601 }, { QStringLiteral("message"), QStringLiteral("Method not found") } });
            send(reply, 0);
        }
        return;
    }
    if (!msg.contains(QStringLiteral("id")))
        return;
    const qint64 id = qint64(msg.value(QStringLiteral("id")).toDouble());
    if (msg.contains(QStringLiteral("error"))) {
        QString text = msg.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().simplified();
        if (text.size() > 200)
            text = text.left(197) + QStringLiteral("…");
        finishPending(id, false, QJsonValue(), text.isEmpty() ? tr("The server returned an error.") : text);
        return;
    }
    finishPending(id, true, msg.value(QStringLiteral("result")), QString());
}

// ---- handshake and tools ---------------------------------------------------------------

void McpConnector::handshake()
{
    const quint64 epoch = m_epoch;
    const QJsonObject params{
        { QStringLiteral("protocolVersion"), QString::fromLatin1(mcp::kProtocolVersion) },
        { QStringLiteral("capabilities"), QJsonObject() },
        { QStringLiteral("clientInfo"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("noch") },
                                                     { QStringLiteral("version"), QStringLiteral(NOCH_VERSION) } } } };
    rpc(QStringLiteral("initialize"), params, initTimeoutMs, [this, epoch](bool ok, const QJsonValue &result, const QString &error) {
        if (epoch != m_epoch)
            return;
        if (!ok) {
            fail(error);
            return;
        }
        const QJsonObject r = result.toObject();
        m_protocol = r.value(QStringLiteral("protocolVersion")).toString(QString::fromLatin1(mcp::kProtocolVersion));
        m_serverName = r.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString();
        notify(QStringLiteral("notifications/initialized"));
        listTools(QString(), {}, {});
    });
}

void McpConnector::listTools(const QString &cursor, QList<ToolDef> acc, QHash<QString, QString> names)
{
    const quint64 epoch = m_epoch;
    QJsonObject params;
    if (!cursor.isEmpty())
        params.insert(QStringLiteral("cursor"), cursor);
    rpc(QStringLiteral("tools/list"), params, initTimeoutMs,
        [this, epoch, acc, names](bool ok, const QJsonValue &result, const QString &error) mutable {
            if (epoch != m_epoch)
                return;
            if (!ok) {
                fail(tr("Could not list the server's tools: %1").arg(error));
                return;
            }
            const QJsonObject r = result.toObject();
            const QString slug = m_spec.id.startsWith(QLatin1String("mcp_")) ? m_spec.id.mid(4) : m_spec.id;
            for (const QJsonValue &tv : r.value(QStringLiteral("tools")).toArray()) {
                const QJsonObject t = tv.toObject();
                const QString orig = t.value(QStringLiteral("name")).toString();
                if (orig.isEmpty() || acc.size() >= 200)
                    continue;
                QStringList taken;
                for (const ToolDef &d : std::as_const(acc))
                    taken << d.name;
                ToolDef d;
                d.name = mcp::exposedName(slug, orig, taken);
                d.title = connectors::sanitizeText(t.value(QStringLiteral("title")).toString(orig), 80).simplified();
                // The description is written by the server's author: cleaned and capped, never trusted as policy.
                d.description = connectors::sanitizeText(
                    QStringLiteral("[%1] %2").arg(m_spec.name, t.value(QStringLiteral("description")).toString()), mcp::kMaxDescription);
                QJsonObject schema = t.value(QStringLiteral("inputSchema")).toObject();
                schema.remove(QStringLiteral("$schema"));
                if (schema.value(QStringLiteral("type")).toString() != QLatin1String("object"))
                    schema = QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") },
                                          { QStringLiteral("properties"), schema.value(QStringLiteral("properties")).toObject() } };
                d.inputSchema = schema;
                const bool readOnly = t.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool(false);
                d.access = readOnly ? Access::Read : Access::Write;
                names.insert(d.name, orig);
                acc.append(d);
            }
            const QString next = r.value(QStringLiteral("nextCursor")).toString();
            if (!next.isEmpty() && acc.size() < 200) {
                listTools(next, acc, names);
                return;
            }
            m_tools = acc;
            m_original = names;
            const QString who = m_serverName.isEmpty() ? m_spec.name : m_serverName;
            setState(connectors::State::Connected, tr("Connected to %1 · %2 tool(s)").arg(who).arg(m_tools.size()), who);
        });
}

void McpConnector::call(const QString &tool, const QJsonObject &args, Done done)
{
    const auto it = m_original.constFind(tool);
    if (!connected() || it == m_original.constEnd()) {
        QTimer::singleShot(0, this, [done, tool] { done(false, QObject::tr("The tool '%1' is not available (the server is not connected).").arg(tool)); });
        return;
    }
    const QJsonObject params{ { QStringLiteral("name"), it.value() }, { QStringLiteral("arguments"), args } };
    const quint64 epoch = m_epoch;
    rpc(QStringLiteral("tools/call"), params, callTimeoutMs, [this, done, epoch](bool ok, const QJsonValue &result, const QString &error) {
        Q_UNUSED(epoch);
        if (!ok) {
            done(false, error);
            return;
        }
        bool isError = false;
        const QString text = mcp::resultText(result.toObject(), &isError);
        done(!isError, text.isEmpty() ? (isError ? tr("The tool reported an error.") : tr("(the tool returned nothing)")) : text);
    });
}
