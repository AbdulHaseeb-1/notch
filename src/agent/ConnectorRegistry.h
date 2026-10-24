// All connectors in one place: the built-in apps (Gmail, GitHub) and any MCP servers
// saved earlier. It is what BuddyProvider and the Settings page talk to.
//
// For Buddy: tools() lists the tools of connected connectors, callTool() runs one, and
// the "always allow" grants live here, so Settings can show and revoke them. For
// Settings (QML): connectorList() describes every connector, and connectApp() /
// disconnectApp() drive the browser sign-ins. The built-in apps sign in with Noch's
// own OAuth client IDs (BuiltinOAuth): there is nothing for the user to paste.
//
// Persistence (only once setStorageDir() was called; tests never touch real files):
//   <dir>/connectors.json   non-secret settings: the MCP servers (name, transport,
//                           command or URL), the "always allow" tools
//   <dir>/secrets/*.sec     everything secret, encrypted (SecretStore): OAuth tokens,
//                           MCP tokens and environment
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>

#include "Connector.h"
#include "SecretStore.h"

class GithubConnector;
class GmailConnector;

class ConnectorRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList connectors READ connectorList NOTIFY changed)
    Q_PROPERTY(bool secretsEncrypted READ secretsEncrypted CONSTANT)
public:
    struct Tool {
        connectors::ToolDef def;
        QString connectorId;
        QString connectorName;
    };

    explicit ConnectorRegistry(QObject *parent = nullptr);

    // Turns persistence on. Call before load().
    void setStorageDir(const QString &dir);
    QString storageDir() const { return m_dir; }
    // Reads the saved settings, restores saved sign-ins and starts the MCP servers that
    // were left connected.
    void load();

    // Takes ownership.
    void addConnector(Connector *c);
    Connector *find(const QString &id) const;
    GmailConnector *gmail() const { return m_gmail; }
    GithubConnector *github() const { return m_github; }
    SecretStore *secrets() { return &m_secrets; }
    bool secretsEncrypted() const { return secrets::encryptionAvailable(); }

    // ---- for Buddy ----------------------------------------------------------------
    QList<Tool> tools() const;
    bool hasTools() const;
    bool findTool(const QString &name, Tool *out = nullptr) const;
    bool hasTool(const QString &name) const { return findTool(name); }
    // The validation entry of a tool, or nullptr.
    const ToolSchema *schemaFor(const QString &name) const;
    void callTool(const QString &name, const QJsonObject &args, connectors::Done done);
    void cancelAll();

    // "Always allow" grants for write tools; saved, shown and revoked in Settings.
    bool alwaysAllowed(const QString &tool) const { return m_always.contains(tool); }
    void setAlwaysAllow(const QString &tool, bool on);
    QStringList alwaysAllowedTools() const { return m_always.values(); }

    // ---- for Settings -------------------------------------------------------------
    QVariantList connectorList() const;
    Q_INVOKABLE void connectApp(const QString &id);
    Q_INVOKABLE void disconnectApp(const QString &id);
    // Adds an MCP server and connects it. Returns an error sentence, or "" when added.
    // (Kept for saved servers and tests; Settings no longer offers adding one — the
    // built-in apps above are the connectors now.)
    // `secrets`: `Header: value` lines (http) or `NAME=value` lines (stdio), kept encrypted.
    Q_INVOKABLE QString addMcpServer(const QString &name, const QString &transport, const QString &target,
                                     const QString &secrets);
    // Same, for an existing server (keeps its id; empty `secrets` keeps the saved ones).
    Q_INVOKABLE QString updateMcpServer(const QString &id, const QString &name, const QString &transport,
                                        const QString &target, const QString &secrets);
    Q_INVOKABLE void removeConnector(const QString &id);
    Q_INVOKABLE void revokeAlwaysAllow(const QString &tool) { setAlwaysAllow(tool, false); }
    Q_INVOKABLE void revokeAllAlwaysAllow();

signals:
    // Anything about a connector changed (for the settings list).
    void changed();
    // The set of tools offered to the model may have changed.
    void toolsChanged();

private:
    void hook(Connector *c);
    void saveConfig();
    QString uniqueMcpId(const QString &name) const;

    QString m_dir;
    SecretStore m_secrets;
    QList<Connector *> m_list;
    GmailConnector *m_gmail = nullptr;
    GithubConnector *m_github = nullptr;
    QSet<QString> m_always;
    bool m_loading = false;
    // tool name -> { definition fingerprint, schema }
    mutable QHash<QString, QPair<QString, ToolSchema>> m_schemaCache;
};
