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

void BuddyProvider::setToolDiet(bool on)
{
    if (m_toolDiet == on)
        return;
    m_toolDiet = on;
    emit agentConfigChanged();
}

bool BuddyProvider::toolSchemaVisible(const QString &toolName) const
{
    // Diet off: every enabled tool's schema is sent (previous behavior).
    if (!m_toolDiet)
        return true;
    // tool_search is the loader: always visible.
    if (toolName == QLatin1String("tool_search"))
        return true;
    // Core tools are always visible.
    static const QStringList coreTools{
        QStringLiteral("bash"), QStringLiteral("str_replace_editor"),
        QStringLiteral("glob_grep"), QStringLiteral("computer"),
        // Interaction tools are core too, not deferrable utilities:
        // asking the user a question and showing/sharing screenshots are
        // the loop's basic channels, and handoffs pick the active
        // specialist. Hiding them by default changes behavior, not just
        // token count — the tests encode "offered by default" for all
        // three. Utility schemas (web, memory, advisor, todo) stay deferred.
        QStringLiteral("ask_user_question"),
        QStringLiteral("share_screenshot"),
        // The music pill is a user-facing channel too ("play some music"): no
        // tool_search round trip first.
        QStringLiteral("music"),
    };
    if (coreTools.contains(toolName))
        return true;
    if (toolName.startsWith(QLatin1String("transfer_to_")))
        return true;
    // A restricted specialist's toolset is its core: the diet must not
    // hide what the active profile is allowed to use (e.g. web_search
    // for the research specialist).
    if (m_activeAgent != QLatin1String("buddy")) {
        const AgentProfile *p = profileFor(m_activeAgent);
        if (p && !p->tools.isEmpty() && p->tools.contains(toolName))
            return true;
    }
    // Deferred tools only once loaded via tool_search this run.
    return m_unlockedDeferredTools.contains(toolName);
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

void BuddyProvider::setAllowScreenshots(bool on)
{
    if (m_allowScreenshots == on)
        return;
    m_allowScreenshots = on;
    emit agentConfigChanged();
}

void BuddyProvider::setAllowMusic(bool on)
{
    if (m_allowMusic == on)
        return;
    m_allowMusic = on;
    emit agentConfigChanged();
}

void BuddyProvider::setMusicBackend(MusicBackend *backend)
{
    if (m_musicBackend == backend)
        return;
    m_musicBackend = backend;
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
                         || !m_agentTodos.isEmpty() || !m_attachments.isEmpty();
    const bool hadAttachments = !m_attachments.isEmpty();
    m_attachments.clear();
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
    m_repairedCalls = 0;
    m_malformedCalls = 0;
    m_totalToolCalls = 0;
    m_repairFailCounts.clear();
    m_repairRateAlerted = false;
    // P5: a plan belongs to one run.
    m_plan.clear();
    m_planActive = false;
    m_planIndex = 0;
    m_consecutivePlanFailures = 0;
    m_replanCount = 0;
    m_planNote.clear();
    m_replanNeeded = false;
    m_pendingProcedure.clear();
    // In-run undo_edit backups hold full original file contents; the durable
    // cross-run undo lives in the RunJournal, so drop them here rather than
    // growing without bound.
    m_fileUndoBackups.clear();
    clearPendingApproval();
    clearPendingQuestion();
    if (hadAttachments)
        emit attachmentsChanged();
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
    meta[QStringLiteral("repairedCalls")] = m_repairedCalls;
    meta[QStringLiteral("malformedCalls")] = m_malformedCalls;
    // P1: repair-rate alerting. If more than ~30% of tool calls needed repair
    // or a correction nudge, the prompt or schemas are likely regressed —
    // flag it once per run in the trace and the response meta.
    if (!m_repairRateAlerted && m_totalToolCalls >= 5) {
        const double rate = double(m_repairedCalls + m_malformedCalls) / double(m_totalToolCalls);
        if (rate > 0.30) {
            m_repairRateAlerted = true;
            meta[QStringLiteral("repairRateAlert")] = true;
            meta[QStringLiteral("repairRate")] = rate;
            addSpan(QJsonObject{ { QStringLiteral("type"), QStringLiteral("repair") },
                                 { QStringLiteral("event"), QStringLiteral("rate-alert") },
                                 { QStringLiteral("rate"), rate },
                                 { QStringLiteral("total"), m_totalToolCalls } });
        }
    }
    meta[QStringLiteral("retries")] = m_retryCount;
    meta[QStringLiteral("traceId")] = m_traceId;
    meta[QStringLiteral("agent")] = m_activeAgent;
    meta[QStringLiteral("agentTitle")] = activeAgentTitle();
    meta[QStringLiteral("handoffs")] = m_handoffCount;
    meta[QStringLiteral("continuations")] = m_continuations;
    meta[QStringLiteral("truncated")] = m_truncated;
    meta[QStringLiteral("maxSteps")] = m_maxSteps;
    meta[QStringLiteral("computerMode")] = computerToolMode();
    if (m_classification.valid)
        meta[QStringLiteral("classifier")] = m_classification.toVariantMap();
    m_responseMeta = meta;
    emit responseMetaChanged();
}

bool BuddyProvider::hasAnyToolEnabled() const
{
    return m_agentMode && !m_toolsDisabledForTurn
           && (m_allowBash || m_allowComputer || m_allowEditor || m_allowCodeSearch
               || m_allowMemory || m_allowWebSearch || m_allowToolSearch
               || m_allowAdvisor || m_allowTodoTask || m_allowAskUser || m_allowScreenshots
               || musicAvailable());
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

