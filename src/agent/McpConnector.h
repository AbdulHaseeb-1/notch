// A Model Context Protocol server as a connector: any app that has an MCP server
// (GitHub, Notion, Slack, Linear, a database ...) works without new C++, because the
// user adds the server in Settings > Connectors and its tools become Buddy's tools.
//
// Two transports, the two the MCP spec defines:
//   stdio  a command Noch starts (for example `npx -y some-mcp-server`); JSON-RPC, one
//          message per line, on its stdin/stdout.
//   http   "Streamable HTTP": POST each JSON-RPC message to one URL; the answer is plain
//          JSON or a short event stream. https only, except for this computer.
// The handshake is `initialize` (we offer 2025-06-18; the server may answer with an
// older version it supports), then `notifications/initialized` and `tools/list`.
// Servers that only speak the newest stateless revision without `initialize` are not
// supported yet.
//
// A tool is read-only only when the server says so (annotations.readOnlyHint); anything
// else is treated as a write and asked about every time. Tool names get the prefix
// `mcp_<server>_`, so a server can never shadow Noch's own tools, and tool descriptions
// (which come from the server) are cleaned and capped before the model sees them.
//
// The secrets text (one line per secret) is kept encrypted (SecretStore):
//   http   `Header-Name: value`    sent with every request, e.g. `Authorization: Bearer ...`
//   stdio  `NAME=value`            added to the server's environment
#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include "Connector.h"

class QNetworkReply;
class SecretStore;

namespace mcp {

struct Spec {
    QString id;          // "mcp_<slug>"
    QString name;        // as the user typed it
    QString transport;   // "stdio" | "http"
    QString target;      // the command line, or the URL
    bool autoConnect = true;

    QJsonObject toJson() const;
    static Spec fromJson(const QJsonObject &o);
    bool valid() const { return !id.isEmpty() && !target.isEmpty() && (transport == QLatin1String("stdio") || transport == QLatin1String("http")); }
};

// "Name: value" / "NAME=value" lines. Blank lines and #comments are skipped.
QList<QPair<QString, QString>> parseHeaderLines(const QString &text);
QList<QPair<QString, QString>> parseEnvLines(const QString &text);
// A command line into program and arguments (quotes honoured).
QStringList splitCommand(const QString &command);
// Complains about a spec the user typed ("" = fine).
QString validateSpec(const QString &transport, const QString &target);
// Tool name the model sees.
QString exposedName(const QString &serverSlug, const QString &toolName, const QStringList &taken);
// The text for the model out of a tools/call result.
QString resultText(const QJsonObject &result, bool *isError);
// One event of an SSE body: the joined `data:` lines of the block, or "" when there is none.
QString sseData(const QByteArray &block);

constexpr const char *kProtocolVersion = "2025-06-18";
constexpr int kMaxDescription = 600;

} // namespace mcp

class McpConnector : public Connector
{
    Q_OBJECT
public:
    McpConnector(const mcp::Spec &spec, SecretStore *secrets, QObject *parent = nullptr);
    ~McpConnector() override;

    QString id() const override { return m_spec.id; }
    QString name() const override { return m_spec.name; }
    QString summary() const override;
    QString icon() const override { return QStringLiteral("layers"); }
    QString kind() const override { return QStringLiteral("mcp"); }
    QList<connectors::Permission> permissions() const override;
    QList<connectors::ToolDef> tools() const override { return connected() ? m_tools : QList<connectors::ToolDef>(); }

    void connectAccount() override;
    void disconnectAccount() override;
    void call(const QString &tool, const QJsonObject &args, connectors::Done done) override;
    void cancelAll() override;

    const mcp::Spec &spec() const { return m_spec; }
    QString secretsKey() const { return QStringLiteral("mcp.%1.secrets").arg(m_spec.id); }

    int initTimeoutMs = 30000;   // npx may download a package on first start
    int callTimeoutMs = 90000;

private:
    using RpcDone = std::function<void(bool ok, const QJsonValue &result, const QString &error)>;
    struct Pending {
        RpcDone done;
        QTimer *timer = nullptr;
    };

    void startTransport();
    void stopTransport();
    void rpc(const QString &method, const QJsonObject &params, int timeoutMs, RpcDone done);
    void notify(const QString &method);
    void send(const QJsonObject &message, qint64 id);
    void onMessage(const QJsonObject &message);
    void finishPending(qint64 id, bool ok, const QJsonValue &result, const QString &error);
    void failAll(const QString &error);
    void fail(const QString &status);
    void handshake();
    void listTools(const QString &cursor, QList<connectors::ToolDef> acc, QHash<QString, QString> names);
    void sendHttp(const QByteArray &payload, qint64 id);
    void onStdioData();

    mcp::Spec m_spec;
    SecretStore *m_secrets;
    QNetworkAccessManager m_nam;
    QPointer<QProcess> m_proc;
    QByteArray m_inbuf;
    QByteArray m_stderrTail;
    QHash<qint64, Pending> m_pending;
    qint64 m_nextId = 1;
    QString m_sessionId;
    QString m_protocol;
    QString m_serverName;
    QList<connectors::ToolDef> m_tools;
    QHash<QString, QString> m_original;   // exposed name -> the server's name
    quint64 m_epoch = 0;
    bool m_stopping = false;
};
