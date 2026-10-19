#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

void BuddyProvider::setProvider(const QString &p)
{
    if (m_provider == p)
        return;
    m_provider = p;
    m_outputTokenLimit = kDefaultOutputTokens;
    m_computerFallback = 0;
    m_enabled = (p != "none");
    emit providerChanged();
    emit enabledChanged();
}

void BuddyProvider::setApiKey(const QString &k)
{
    if (m_apiKey == k)
        return;
    m_apiKey = k;
    emit apiKeyChanged();
}

void BuddyProvider::setApiBase(const QString &b)
{
    if (m_apiBase == b)
        return;
    m_apiBase = b;
    emit apiBaseChanged();
}

void BuddyProvider::setModel(const QString &m)
{
    if (m_model == m)
        return;
    m_model = m;
    m_outputTokenLimit = kDefaultOutputTokens;
    m_computerFallback = 0;
    emit modelChanged();
}

void BuddyProvider::setTemperature(double t)
{
    if (qFuzzyCompare(m_temperature, t))
        return;
    m_temperature = t;
    emit temperatureChanged();
}

void BuddyProvider::setAgentMode(bool on)
{
    if (m_agentMode == on)
        return;
    m_agentMode = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowBash(bool on)
{
    if (m_allowBash == on)
        return;
    m_allowBash = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowComputer(bool on)
{
    if (m_allowComputer == on)
        return;
    m_allowComputer = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowEditor(bool on)
{
    if (m_allowEditor == on)
        return;
    m_allowEditor = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowCodeSearch(bool on)
{
    if (m_allowCodeSearch == on)
        return;
    m_allowCodeSearch = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowMemory(bool on)
{
    if (m_allowMemory == on)
        return;
    m_allowMemory = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowWebSearch(bool on)
{
    if (m_allowWebSearch == on)
        return;
    m_allowWebSearch = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowToolSearch(bool on)
{
    if (m_allowToolSearch == on)
        return;
    m_allowToolSearch = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowAdvisor(bool on)
{
    if (m_allowAdvisor == on)
        return;
    m_allowAdvisor = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowTodoTask(bool on)
{
    if (m_allowTodoTask == on)
        return;
    m_allowTodoTask = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowAskUser(bool on)
{
    if (m_allowAskUser == on)
        return;
    m_allowAskUser = on;
    emit agentConfigChanged();
}

QString BuddyProvider::memoriesPath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.noch");
    const QString dirPath = QDir(base).filePath(QStringLiteral("memories"));
    QDir().mkpath(dirPath);
    return dirPath;
}

void BuddyProvider::clearToolSteps()
{
    const bool hadData = !m_toolSteps.isEmpty() || !m_activeTool.isEmpty()
                         || !m_responseMeta.isEmpty() || !m_citations.isEmpty()
                         || !m_agentTodos.isEmpty();
    m_toolSteps.clear();
    m_responseMeta.clear();
    m_citations.clear();
    m_agentTodos.clear();
    m_totalInputTokens = 0;
    m_totalOutputTokens = 0;
    m_lastModelUsed.clear();
    m_lastStopReason.clear();
    m_advisorUsedInTurn = false;
    m_advisorFocusInTurn.clear();
    m_advisorInsightInTurn.clear();
    m_unlockedDeferredTools.clear();
    m_callRepeats.clear();
    m_lastComputerFp.clear();
    m_computerRepeat = 0;
    m_compactedResults = 0;
    m_cachedTokens = 0;
    m_deniedCalls = 0;
    clearPendingApproval();
    clearPendingQuestion();
    if (hadData) {
        emit toolStepsChanged();
        emit responseMetaChanged();
        emit agentTodosChanged();
    }
    setActiveToolState(QString(), QString());
}

void BuddyProvider::setActiveToolState(const QString &tool, const QString &summary)
{
    const QString badge = tool.isEmpty() ? QString() : badgeForTool(tool, QJsonObject());
    if (m_activeTool == tool && m_activeToolBadge == badge && m_activeToolSummary == summary)
        return;
    m_activeTool = tool;
    m_activeToolBadge = badge;
    m_activeToolSummary = summary;
    emit activeToolChanged();
}

void BuddyProvider::addCitation(const QString &title, const QString &url)
{
    const QString cleanUrl = url.trimmed();
    if (cleanUrl.isEmpty() || !cleanUrl.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        return;
    for (const QVariant &v : m_citations) {
        if (v.toMap().value(QStringLiteral("url")).toString() == cleanUrl)
            return;
    }
    if (m_citations.size() >= 8)
        return;
    const QUrl parsed(cleanUrl);
    QString domain = parsed.host();
    if (domain.startsWith(QLatin1String("www.")))
        domain = domain.mid(4);
    QVariantMap item;
    item[QStringLiteral("title")] = title.trimmed().isEmpty() ? domain : title.trimmed();
    item[QStringLiteral("url")] = cleanUrl;
    item[QStringLiteral("domain")] = domain.isEmpty() ? cleanUrl : domain;
    m_citations.append(item);
}

void BuddyProvider::updateResponseMeta()
{
    QVariantMap meta;
    meta[QStringLiteral("sdk")] = QStringLiteral("Anthropic Agent SDK");
    meta[QStringLiteral("provider")] = m_provider;
    meta[QStringLiteral("model")] = m_lastModelUsed.isEmpty() ? m_model : m_lastModelUsed;
    meta[QStringLiteral("stopReason")] = m_lastStopReason;
    meta[QStringLiteral("inputTokens")] = m_totalInputTokens;
    meta[QStringLiteral("outputTokens")] = m_totalOutputTokens;
    meta[QStringLiteral("totalTokens")] = m_totalInputTokens + m_totalOutputTokens;
    meta[QStringLiteral("turns")] = qMax(1, m_agentIteration + 1);
    meta[QStringLiteral("toolCount")] = m_toolSteps.size();
    meta[QStringLiteral("durationMs")] = m_turnTimer.isValid() ? int(m_turnTimer.elapsed()) : 0;
    meta[QStringLiteral("advisorUsed")] = m_advisorUsedInTurn;
    meta[QStringLiteral("advisorFocus")] = m_advisorFocusInTurn;
    meta[QStringLiteral("advisorInsight")] = m_advisorInsightInTurn;
    meta[QStringLiteral("permissionMode")] = m_permissionMode;
    meta[QStringLiteral("cachedTokens")] = m_cachedTokens;
    meta[QStringLiteral("compacted")] = m_compactedResults;
    meta[QStringLiteral("denied")] = m_deniedCalls;
    meta[QStringLiteral("retries")] = m_retryCount;
    meta[QStringLiteral("traceId")] = m_traceId;
    meta[QStringLiteral("agent")] = m_activeAgent;
    meta[QStringLiteral("agentTitle")] = activeAgentTitle();
    meta[QStringLiteral("handoffs")] = m_handoffCount;
    meta[QStringLiteral("continuations")] = m_continuations;
    meta[QStringLiteral("truncated")] = m_truncated;
    meta[QStringLiteral("maxSteps")] = m_maxSteps;
    meta[QStringLiteral("computerMode")] = computerToolMode();
    m_responseMeta = meta;
    emit responseMetaChanged();
}

bool BuddyProvider::hasAnyToolEnabled() const
{
    return m_agentMode && !m_toolsDisabledForTurn
           && (m_allowBash || m_allowComputer || m_allowEditor || m_allowCodeSearch
               || m_allowMemory || m_allowWebSearch || m_allowToolSearch
               || m_allowAdvisor || m_allowTodoTask || m_allowAskUser);
}

bool BuddyProvider::needsApiKey() const
{
    return m_provider == QLatin1String("openai") || m_provider == QLatin1String("anthropic");
}

QString BuddyProvider::trimBase(const QString &base)
{
    return cleanBase(base);
}

QString BuddyProvider::defaultBase() const
{
    return defaultBaseFor(m_provider);
}

QString BuddyProvider::chatUrl() const
{
    const QString base = m_apiBase.trimmed().isEmpty() ? defaultBaseFor(m_provider)
                                                       : m_apiBase;
    return joinPath(base, chatPathFor(m_provider));
}

QString BuddyProvider::modelsUrl() const
{
    const QString base = m_apiBase.trimmed().isEmpty() ? defaultBaseFor(m_provider)
                                                       : m_apiBase;
    return joinPath(base, modelsPathFor(m_provider));
}

QString BuddyProvider::buildUrl() const
{
    return chatUrl();
}

