#include "SessionLink.h"

#include "BuddyProvider.h"
#include "ChatSessions.h"

SessionLink::SessionLink(ChatSessions *sessions, BuddyProvider *provider, QObject *parent)
    : QObject(parent), m_sessions(sessions), m_provider(provider)
{
    connect(sessions, &ChatSessions::summaryWanted, this, [this](const QString &id, int keep) {
        const QVariantMap job = m_sessions->summaryJob(id, keep);
        if (job.isEmpty())
            return;
        m_provider->summarizeSession(id, job.value(QStringLiteral("prior")).toString(),
                                     job.value(QStringLiteral("turns")).toList(), job.value(QStringLiteral("upTo")).toInt());
    });
    connect(provider, &BuddyProvider::sessionSummaryReady, this,
            [this](const QString &id, const QString &summary, int upTo) { m_sessions->setSummary(id, summary, upTo); });
    connect(provider, &BuddyProvider::sessionSummaryFailed, this,
            [this](const QString &id, const QString &) { m_sessions->noteSummaryFailed(id); });
}
