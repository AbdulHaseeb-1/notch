// Buddy's side of the connected apps: loading them, telling the permission gate which of
// their tools only read and which change something, and running a tool call. The apps
// themselves live in ConnectorRegistry / Connector (see docs/connectors.md).
#include "BuddyProviderInternal.h"

#include "AppStorage.h"

using namespace noch::buddyprovider;

void BuddyProvider::startConnectors()
{
    // Same directory as noch.ini (see AppStorage). An older build kept this
    // tree under AppDataLocation; import that once, without replacing
    // anything already in the new folder.
    noch::storage::importLegacyConnectors();
    m_connectors->setStorageDir(noch::storage::connectorsDir());
    m_connectors->load();
    syncConnectorPolicy();
}

void BuddyProvider::syncConnectorPolicy()
{
    QHash<QString, bool> writes;
    if (m_allowConnectors)
        for (const ConnectorRegistry::Tool &t : m_connectors->tools())
            writes.insert(t.def.name, t.def.access == connectors::Access::Write);
    m_policy.setConnectorTools(writes);
    QSet<QString> always;
    for (const QString &t : m_connectors->alwaysAllowedTools())
        always.insert(t);
    m_policy.setConnectorAlwaysAllow(always);
}

void BuddyProvider::executeConnectorTool(const PendingToolCall &call)
{
    const quint64 generation = m_runGeneration;
    m_connectors->callTool(call.name, call.input, [this, call, generation](bool ok, const QString &text) {
        // The run was cancelled or replaced while the app was answering: drop the answer.
        if (generation != m_runGeneration)
            return;
        finishCurrentToolCall(call, text, QString(), !ok);
    });
}
