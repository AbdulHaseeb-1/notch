#include "ConnectorRegistry.h"

#include "GmailConnector.h"
#include "McpConnector.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QVariantMap>

using connectors::Access;

ConnectorRegistry::ConnectorRegistry(QObject *parent)
    : QObject(parent)
{
    m_gmail = new GmailConnector(this);
    m_gmail->setSecretStore(&m_secrets);
    addConnector(m_gmail);
}

void ConnectorRegistry::setStorageDir(const QString &dir)
{
    m_dir = dir;
    m_secrets.setDir(dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("secrets")));
}

void ConnectorRegistry::hook(Connector *c)
{
    connect(c, &Connector::changed, this, [this] {
        m_schemaCache.clear();
        emit changed();
        emit toolsChanged();
    });
}

void ConnectorRegistry::addConnector(Connector *c)
{
    c->setParent(this);
    m_list.append(c);
    hook(c);
    m_schemaCache.clear();
    emit changed();
    emit toolsChanged();
}

Connector *ConnectorRegistry::find(const QString &id) const
{
    for (Connector *c : m_list)
        if (c->id() == id)
            return c;
    return nullptr;
}

// ---- persistence ----------------------------------------------------------------------

void ConnectorRegistry::saveConfig()
{
    if (m_dir.isEmpty() || m_loading)
        return;
    QJsonArray servers;
    for (Connector *c : m_list)
        if (auto *m = qobject_cast<McpConnector *>(c))
            servers.append(m->spec().toJson());
    QJsonArray always;
    QStringList sorted = m_always.values();
    sorted.sort();
    for (const QString &t : sorted)
        always.append(t);
    const QJsonObject root{ { QStringLiteral("version"), 1 },
                            { QStringLiteral("gmail"), QJsonObject{ { QStringLiteral("clientId"), m_gmail->clientId() } } },
                            { QStringLiteral("mcp"), servers },
                            { QStringLiteral("alwaysAllow"), always } };
    QDir().mkpath(m_dir);
    QSaveFile f(QDir(m_dir).filePath(QStringLiteral("connectors.json")));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

void ConnectorRegistry::load()
{
    if (m_dir.isEmpty())
        return;
    m_loading = true;
    QFile f(QDir(m_dir).filePath(QStringLiteral("connectors.json")));
    QJsonObject root;
    if (f.open(QIODevice::ReadOnly) && f.size() < (1 << 20))
        root = QJsonDocument::fromJson(f.readAll()).object();

    m_always.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("alwaysAllow")).toArray())
        if (!v.toString().isEmpty())
            m_always.insert(v.toString());

    m_gmail->setClientCredentials(root.value(QStringLiteral("gmail")).toObject().value(QStringLiteral("clientId")).toString(),
                                  m_secrets.get(QStringLiteral("gmail.client_secret")));
    m_gmail->restore();

    QList<McpConnector *> toStart;
    for (const QJsonValue &v : root.value(QStringLiteral("mcp")).toArray()) {
        const mcp::Spec spec = mcp::Spec::fromJson(v.toObject());
        if (!spec.valid() || find(spec.id))
            continue;
        auto *m = new McpConnector(spec, &m_secrets, this);
        addConnector(m);
        if (spec.autoConnect)
            toStart << m;
    }
    m_loading = false;
    // Start after the event loop turns: a server that takes a while must not delay start-up.
    for (McpConnector *m : std::as_const(toStart))
        QTimer::singleShot(0, m, [m] { m->connectAccount(); });
    emit changed();
    emit toolsChanged();
}

// ---- for Buddy ------------------------------------------------------------------------------

QList<ConnectorRegistry::Tool> ConnectorRegistry::tools() const
{
    QList<Tool> out;
    QSet<QString> seen;
    for (Connector *c : m_list) {
        if (!c->connected())
            continue;
        for (const connectors::ToolDef &d : c->tools()) {
            if (seen.contains(d.name))
                continue;   // the first connector keeps a name two of them claim
            seen.insert(d.name);
            out.append({ d, c->id(), c->name() });
        }
    }
    return out;
}

bool ConnectorRegistry::hasTools() const
{
    for (Connector *c : m_list)
        if (c->connected() && !c->tools().isEmpty())
            return true;
    return false;
}

bool ConnectorRegistry::findTool(const QString &name, Tool *out) const
{
    if (name.isEmpty())
        return false;
    for (Connector *c : m_list) {
        if (!c->connected())
            continue;
        for (const connectors::ToolDef &d : c->tools())
            if (d.name == name) {
                if (out)
                    *out = { d, c->id(), c->name() };
                return true;
            }
    }
    return false;
}

const ToolSchema *ConnectorRegistry::schemaFor(const QString &name) const
{
    Tool t;
    if (!findTool(name, &t))
        return nullptr;
    // Cached by tool name; rebuilt when the definition changed.
    const QString key = QString::fromUtf8(QJsonDocument(t.def.inputSchema).toJson(QJsonDocument::Compact));
    auto it = m_schemaCache.find(name);
    if (it == m_schemaCache.end() || it->first != key)
        it = m_schemaCache.insert(name, { key, connectors::toToolSchema(t.def) });
    return &it->second;
}

void ConnectorRegistry::callTool(const QString &name, const QJsonObject &args, connectors::Done done)
{
    for (Connector *c : m_list) {
        if (!c->connected())
            continue;
        for (const connectors::ToolDef &d : c->tools())
            if (d.name == name) {
                c->call(name, args, std::move(done));
                return;
            }
    }
    QTimer::singleShot(0, this, [done, name] { done(false, tr("The tool '%1' is not available: its app is not connected.").arg(name)); });
}

void ConnectorRegistry::cancelAll()
{
    for (Connector *c : m_list)
        c->cancelAll();
}

void ConnectorRegistry::setAlwaysAllow(const QString &tool, bool on)
{
    if (on == m_always.contains(tool))
        return;
    if (on)
        m_always.insert(tool);
    else
        m_always.remove(tool);
    saveConfig();
    emit changed();
    emit toolsChanged();
}

void ConnectorRegistry::revokeAllAlwaysAllow()
{
    if (m_always.isEmpty())
        return;
    m_always.clear();
    saveConfig();
    emit changed();
    emit toolsChanged();
}

// ---- for Settings ---------------------------------------------------------------------------

QVariantList ConnectorRegistry::connectorList() const
{
    QVariantList out;
    for (Connector *c : m_list) {
        QVariantList perms;
        for (const connectors::Permission &p : c->permissions())
            perms << QVariantMap{ { QStringLiteral("label"), p.label }, { QStringLiteral("write"), p.access == Access::Write } };
        QVariantList tools;
        for (const connectors::ToolDef &d : c->tools())
            tools << QVariantMap{ { QStringLiteral("name"), d.name }, { QStringLiteral("title"), d.title },
                                  { QStringLiteral("write"), d.access == Access::Write },
                                  { QStringLiteral("always"), m_always.contains(d.name) } };
        QVariantMap m{ { QStringLiteral("id"), c->id() }, { QStringLiteral("name"), c->name() },
                       { QStringLiteral("summary"), c->summary() }, { QStringLiteral("icon"), c->icon() },
                       { QStringLiteral("kind"), c->kind() }, { QStringLiteral("state"), connectors::stateName(c->state()) },
                       { QStringLiteral("status"), c->statusText() }, { QStringLiteral("account"), c->account() },
                       { QStringLiteral("configured"), c->configured() }, { QStringLiteral("permissions"), perms },
                       { QStringLiteral("tools"), tools } };
        if (auto *g = qobject_cast<GmailConnector *>(c)) {
            m.insert(QStringLiteral("clientId"), g->clientId());
            m.insert(QStringLiteral("hasClientSecret"), g->hasClientSecret());
        }
        if (auto *mc = qobject_cast<McpConnector *>(c)) {
            m.insert(QStringLiteral("transport"), mc->spec().transport);
            m.insert(QStringLiteral("target"), mc->spec().target);
            m.insert(QStringLiteral("hasSecrets"), !m_secrets.get(mc->secretsKey()).isEmpty());
        }
        out << m;
    }
    return out;
}

void ConnectorRegistry::connectApp(const QString &id)
{
    if (Connector *c = find(id)) {
        c->connectAccount();
        saveConfig();   // an MCP server that was switched on comes back after a restart
    }
}

void ConnectorRegistry::disconnectApp(const QString &id)
{
    if (Connector *c = find(id)) {
        c->disconnectAccount();
        saveConfig();
    }
}

void ConnectorRegistry::setGoogleClientId(const QString &clientId)
{
    m_gmail->setClientCredentials(clientId, m_secrets.get(QStringLiteral("gmail.client_secret")));
    saveConfig();
}

void ConnectorRegistry::setGoogleClientSecret(const QString &secret)
{
    m_gmail->setClientCredentials(m_gmail->clientId(), secret);
}

QString ConnectorRegistry::uniqueMcpId(const QString &name) const
{
    const QString base = QStringLiteral("mcp_") + connectors::slugify(name);
    QString id = base;
    int n = 2;
    while (find(id))
        id = QStringLiteral("%1_%2").arg(base).arg(n++);
    return id;
}

QString ConnectorRegistry::addMcpServer(const QString &name, const QString &transport, const QString &target,
                                        const QString &secretsText)
{
    const QString nm = name.trimmed().left(40);
    if (nm.isEmpty())
        return tr("Give the server a name.");
    const QString bad = mcp::validateSpec(transport, target);
    if (!bad.isEmpty())
        return bad;
    mcp::Spec spec;
    spec.id = uniqueMcpId(nm);
    spec.name = nm;
    spec.transport = transport;
    spec.target = target.trimmed();
    spec.autoConnect = true;
    if (!secretsText.trimmed().isEmpty())
        m_secrets.set(QStringLiteral("mcp.%1.secrets").arg(spec.id), secretsText);
    auto *m = new McpConnector(spec, &m_secrets, this);
    addConnector(m);
    saveConfig();
    m->connectAccount();
    return QString();
}

QString ConnectorRegistry::updateMcpServer(const QString &id, const QString &name, const QString &transport,
                                           const QString &target, const QString &secretsText)
{
    auto *old = qobject_cast<McpConnector *>(find(id));
    if (!old)
        return tr("That server no longer exists.");
    const QString nm = name.trimmed().left(40);
    if (nm.isEmpty())
        return tr("Give the server a name.");
    const QString bad = mcp::validateSpec(transport, target);
    if (!bad.isEmpty())
        return bad;
    mcp::Spec spec = old->spec();
    spec.name = nm;
    spec.transport = transport;
    spec.target = target.trimmed();
    old->disconnectAccount();
    m_list.removeAll(old);
    old->deleteLater();
    if (!secretsText.trimmed().isEmpty())
        m_secrets.set(QStringLiteral("mcp.%1.secrets").arg(id), secretsText);
    auto *m = new McpConnector(spec, &m_secrets, this);
    addConnector(m);
    saveConfig();
    m->connectAccount();
    return QString();
}

void ConnectorRegistry::removeConnector(const QString &id)
{
    auto *m = qobject_cast<McpConnector *>(find(id));
    if (!m)
        return;   // built-in apps are disconnected, not removed
    m->disconnectAccount();
    m_list.removeAll(m);
    m_secrets.remove(m->secretsKey());
    // Its "always allow" grants go with it.
    const QString prefix = QStringLiteral("mcp_%1_").arg(id.mid(4));
    const QSet<QString> before = m_always;
    for (const QString &t : before)
        if (t.startsWith(prefix))
            m_always.remove(t);
    m->deleteLater();
    saveConfig();
    m_schemaCache.clear();
    emit changed();
    emit toolsChanged();
}

QVariantList ConnectorRegistry::presets() const
{
    return {
        QVariantMap{ { QStringLiteral("name"), QStringLiteral("GitHub") },
                     { QStringLiteral("transport"), QStringLiteral("http") },
                     { QStringLiteral("target"), QStringLiteral("https://api.githubcopilot.com/mcp/") },
                     { QStringLiteral("secretsHint"), QStringLiteral("Authorization: Bearer <your GitHub personal access token>") },
                     { QStringLiteral("help"), tr("GitHub's own MCP server. Create a fine-grained personal access token at github.com/settings/tokens with only the repositories and permissions you want Buddy to have.") } },
    };
}
