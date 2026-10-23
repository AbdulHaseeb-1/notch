// Connectors: the user's accounts in other apps, as tools for Buddy.
//
// A connector is one connected app (Gmail, or any MCP server the user added). It is
// connected once from Settings > Connectors; while connected its tools are offered to
// the model next to bash, web_fetch and the rest, and every call goes through the same
// permission gate:
//   Read   lists, searches, reads. Runs without asking (unless the run saw a prompt
//          injection, then it asks).
//   Write  creates, sends, deletes, anything that changes the account. Asked about
//          every time; only a saved "always allow" for that very tool skips the card.
// What a connector returns (an email, a ticket, a page) is written by other people:
// it is fenced as <untrusted_content> and scanned for injections like web pages are.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "ToolSchemas.h"

namespace connectors {

enum class Access { Read, Write };

struct ToolDef {
    QString name;           // what the model calls, unique across connectors: "gmail_search_messages"
    QString title;          // for the settings list: "Search mail"
    QString description;
    QJsonObject inputSchema;   // JSON Schema of the arguments ({type:object, properties, required})
    Access access = Access::Read;
};

// What a connector can do, in words, for the settings page.
struct Permission {
    QString label;
    Access access = Access::Read;
};

enum class State { Disconnected, Connecting, Connected, Error };
QString stateName(State s);

// Called exactly once with the text the model gets back; `ok` false = an error text.
using Done = std::function<void(bool ok, const QString &text)>;

// ---- helpers shared by every connector and by the gate ---------------------------

// The registry entry for validation and repair of a call (argument types, required
// arguments), built from the tool's JSON Schema.
ToolSchema toToolSchema(const ToolDef &def);
// Text from outside, made safe to show or hand to the model: invisible and control
// characters dropped, whitespace tidied (newlines kept), cut to `maxChars`.
QString sanitizeText(const QString &text, int maxChars);
// "gmail_create_draft" -> "Gmail: create draft"; "mcp_github_list_issues" -> "github: list issues".
QString prettyToolName(const QString &name);
// "mail" for gmail_*, the connector-wide fallback otherwise.
QString iconForConnectorTool(const QString &name);
// Arguments as "key: value" lines for an approval card, values cut to `maxValue`.
QString describeArguments(const QJsonObject &input, int maxValue = 400);
// True for a host that is this computer.
bool isLoopbackHost(const QString &host);
// A id from a display name: lower case, [a-z0-9_], never empty.
QString slugify(const QString &name);

} // namespace connectors

class Connector : public QObject
{
    Q_OBJECT
public:
    explicit Connector(QObject *parent = nullptr) : QObject(parent) {}

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString summary() const = 0;
    virtual QString icon() const { return QStringLiteral("bolt"); }
    // "oauth" (a built-in app) or "mcp" (a server the user added).
    virtual QString kind() const = 0;
    virtual QList<connectors::Permission> permissions() const = 0;
    // The tools offered to the model: empty unless connected.
    virtual QList<connectors::ToolDef> tools() const = 0;
    // True when everything needed to connect is set (for Gmail: the Google client ID).
    virtual bool configured() const { return true; }

    virtual void connectAccount() = 0;
    virtual void disconnectAccount() = 0;
    // Runs one tool. Always answers through `done`, even for an unknown tool or an error.
    virtual void call(const QString &tool, const QJsonObject &args, connectors::Done done) = 0;
    // Stops whatever is in flight (a sign-in waiting for the browser, a request).
    virtual void cancelAll() {}

    connectors::State state() const { return m_state; }
    bool connected() const { return m_state == connectors::State::Connected; }
    // One line for the settings page: "Connected as me@gmail.com", "Waiting for the browser…".
    QString statusText() const { return m_status; }
    QString account() const { return m_account; }

signals:
    // State, status or the tool list changed.
    void changed();

protected:
    void setState(connectors::State state, const QString &status, const QString &account = QString());

private:
    connectors::State m_state = connectors::State::Disconnected;
    QString m_status;
    QString m_account;
};
