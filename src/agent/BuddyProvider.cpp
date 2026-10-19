#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

BuddyProvider::BuddyProvider(QObject *parent)
    : QObject(parent)
{
    connect(&m_nam, &QNetworkAccessManager::finished,
            this, &BuddyProvider::onReplyFinished);

    // Computer use: the HUD Stop button, Ctrl+Alt+Esc or a held Esc end the
    // whole run at once; a finished run hands the screen back.
    m_computer = new ComputerUse(this);
    connect(m_computer, &ComputerUse::stopRequested, this, [this](const QString &why) {
        // Only an active session can ask to stop, and a session ends with
        // its run, so there is always a run to end here.
        addSpan(QJsonObject{ { "type", "computer" }, { "event", "stopped_by_user" } });
        cancel();
        emit errorOccurred(why);
    });
    connect(this, &BuddyProvider::statusChanged, this, [this](const QString &st) {
        if (st == QLatin1String("idle")) {
            m_computer->end();
            finalizeJournal();   // after cancel() has killed anything still running
        }
    });
    m_journal.setRoot(undoPath());
    m_savedRules.setPath(savedRulesPath());
    m_policy.setSavedRules(m_savedRules.rules());

    m_approvalTimer.setInterval(1000);
    connect(&m_approvalTimer, &QTimer::timeout, this, &BuddyProvider::onApprovalTick);
    m_questionTimer.setInterval(1000);
    connect(&m_questionTimer, &QTimer::timeout, this, &BuddyProvider::onQuestionTick);
    m_streamEmitTimer.setSingleShot(true);
    m_streamEmitTimer.setInterval(40);
    connect(&m_streamEmitTimer, &QTimer::timeout, this, [this] { emit streamTextChanged(); });
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &BuddyProvider::dispatchAgentTurn);
    m_policy.setWorkspaceRoots({ effectiveWorkspace() });
    syncPrivatePaths();
}

// ---- safety settings -------------------------------------------------------

void BuddyProvider::setPermissionMode(const QString &m)
{
    const QString norm = AgentPolicy::modeToString(AgentPolicy::modeFromString(m));
    if (norm == m_permissionMode)
        return;
    m_permissionMode = norm;
    m_policy.setMode(AgentPolicy::modeFromString(norm));
    // A mode change is a fresh trust decision: drop "always allow" grants.
    m_policy.clearSessionGrants();
    emit agentConfigChanged();
}

void BuddyProvider::setWorkspaceRoot(const QString &r)
{
    if (r == m_workspaceRoot)
        return;
    m_workspaceRoot = r;
    m_policy.setWorkspaceRoots({ effectiveWorkspace() });
    syncPrivatePaths();
    emit agentConfigChanged();
}

void BuddyProvider::setRedactSecrets(bool on)
{
    if (on == m_redactSecrets)
        return;
    m_redactSecrets = on;
    emit agentConfigChanged();
}

void BuddyProvider::setOfficialComputerTools(bool on)
{
    if (on == m_officialComputer)
        return;
    m_officialComputer = on;
    m_computerFallback = 0;
    emit agentConfigChanged();
}

void BuddyProvider::setComputerDetail(const QString &d)
{
    const QString norm = d == QLatin1String("standard") ? d : QStringLiteral("sharp");
    if (norm == m_computerDetail)
        return;
    m_computerDetail = norm;
    emit agentConfigChanged();
}

void BuddyProvider::setMaxSteps(int n)
{
    n = qBound(kMinMaxSteps, n, kMaxMaxSteps);
    if (n == m_maxSteps)
        return;
    m_maxSteps = n;
    emit agentConfigChanged();
}

// Newest tool version each model accepts (Anthropic's computer-use docs).
// Anything not listed is treated as new: the 400 fallback covers a miss.
BuddyProvider::ComputerMode BuddyProvider::computerModeForModel(const QString &model)
{
    QString m = model.toLower();
    m.replace(QLatin1Char('.'), QLatin1Char('-'));
    static const QStringList v20251124{
        "opus-4-5", "opus-4-6", "opus-4-7", "sonnet-4-6" };
    for (const QString &k : v20251124)
        if (m.contains(k))
            return ComputerMode::V20251124;
    static const QStringList v20250124{
        "sonnet-4-5", "haiku-4-5", "opus-4-1", "claude-3" };
    for (const QString &k : v20250124)
        if (m.contains(k))
            return ComputerMode::V20250124;
    // Dated Claude 4 ids: claude-sonnet-4-20250514, claude-opus-4-20250514.
    static const QRegularExpression claude4Dated(QStringLiteral("(sonnet|opus)-4-20\\d{6}"));
    if (claude4Dated.match(m).hasMatch())
        return ComputerMode::V20250124;
    return ComputerMode::Toolset;
}

QString BuddyProvider::computerModeName(ComputerMode m)
{
    switch (m) {
    case ComputerMode::Toolset: return QStringLiteral("toolset");
    case ComputerMode::V20251124: return QStringLiteral("computer_20251124");
    case ComputerMode::V20250124: return QStringLiteral("computer_20250124");
    case ComputerMode::Custom: break;
    }
    return QStringLiteral("custom");
}

BuddyProvider::ComputerMode BuddyProvider::computerMode() const
{
    if (m_provider != QLatin1String("anthropic") || !m_officialComputer)
        return ComputerMode::Custom;
    return ComputerMode(qMax(int(computerModeForModel(m_model)), m_computerFallback));
}

// Screenshot size and encoding follow the tool version: the toolset (every
// model that takes it) accepts up to 2576 px / 4784 visual tokens and rejects
// anything bigger instead of downscaling, so the screenshot space has to be
// chosen to fit. Older models stay within 1568 px / ~1.15 MP.
void BuddyProvider::applyComputerProfile() const
{
    const bool sharp = m_computerDetail != QLatin1String("standard");
    cu::ImageLimits lim = cu::standardLimits();
    bool lossless = false;
    switch (computerMode()) {
    case ComputerMode::Toolset:
        if (sharp) { lim = cu::sharpLimits(); lossless = true; }
        break;
    case ComputerMode::V20251124:
    case ComputerMode::V20250124:
        if (sharp) { lim = cu::legacySharpLimits(); lossless = true; }
        break;
    case ComputerMode::Custom:
        break;
    }
    m_computer->mapper().setLimits(lim);
    m_computer->setLosslessScreenshots(lossless);
}

// The older computer tool versions are beta tools: the header goes on a
// request only when it actually carries one. The toolset needs none.
void BuddyProvider::addBetaHeaders(QNetworkRequest &request, const QJsonObject &payload) const
{
    if (m_provider != QLatin1String("anthropic"))
        return;
    QStringList betas;
    for (const QJsonValue &t : payload.value(QStringLiteral("tools")).toArray()) {
        const QString type = t.toObject().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("computer_20251124") && !betas.contains(QStringLiteral("computer-use-2025-11-24")))
            betas << QStringLiteral("computer-use-2025-11-24");
        else if (type == QLatin1String("computer_20250124") && !betas.contains(QStringLiteral("computer-use-2025-01-24")))
            betas << QStringLiteral("computer-use-2025-01-24");
    }
    if (!betas.isEmpty())
        request.setRawHeader("anthropic-beta", betas.join(QLatin1Char(',')).toUtf8());
}

bool BuddyProvider::historyHasComputerToolUse() const
{
    for (const QJsonValue &mv : m_agentMessages) {
        const QJsonObject m = mv.toObject();
        if (m.value(QStringLiteral("role")).toString() != QLatin1String("assistant"))
            continue;
        const QJsonValue c = m.value(QStringLiteral("content"));
        if (!c.isArray())
            continue;
        for (const QJsonValue &bv : c.toArray()) {
            const QJsonObject b = bv.toObject();
            if (b.value(QStringLiteral("type")).toString() == QLatin1String("tool_use")
                    && (b.contains(QStringLiteral("toolset_name"))
                        || b.value(QStringLiteral("name")).toString() == QLatin1String("computer")))
                return true;
        }
    }
    return false;
}

bool BuddyProvider::mentionsComputerTool(const QString &error)
{
    const QString e = error.toLower();
    return e.contains(QLatin1String("computer")) || e.contains(QLatin1String("toolset"))
        || e.contains(QLatin1String("anthropic-beta"));
}

void BuddyProvider::setStreamReplies(bool on)
{
    if (on == m_streamReplies)
        return;
    m_streamReplies = on;
    emit agentConfigChanged();
}

// ---- handoffs ------------------------------------------------------------------
// Modelled on OpenAI Agents SDK handoffs: each handoff is exposed to the model
// as a `transfer_to_<agent>` tool; the receiving agent sees the whole
// conversation but runs with its own instructions and a narrower toolset.
// The permission policy still gates every call.

const QList<BuddyProvider::AgentProfile> &BuddyProvider::agentProfiles()
{
    static const QList<AgentProfile> profiles{
        { QStringLiteral("buddy"), QStringLiteral("Buddy"), QStringLiteral("spark"), {},
          QStringLiteral("Hand back to Buddy, the generalist, when the specialist part is done or the task needs other tools."),
          QString() },
        { QStringLiteral("research"), QStringLiteral("Research specialist"), QStringLiteral("globe"),
          { QStringLiteral("web_search"), QStringLiteral("web_fetch"), QStringLiteral("memory"),
            QStringLiteral("todo_write"), QStringLiteral("advisor"), QStringLiteral("ask_user_question") },
          QStringLiteral("Questions that need current information from the web, comparing sources, or a cited summary."),
          QStringLiteral("You are Noch's research specialist. Search first, open the two or three most relevant sources, "
                         "cross-check claims, and answer with short inline citations (site names). Say when sources "
                         "disagree or are thin. You cannot run commands, edit files or control the computer.") },
        { QStringLiteral("code"), QStringLiteral("Code specialist"), QStringLiteral("code-search"),
          { QStringLiteral("glob_grep"), QStringLiteral("str_replace_editor"), QStringLiteral("bash"),
            QStringLiteral("todo_write"), QStringLiteral("memory"), QStringLiteral("ask_user_question") },
          QStringLiteral("Reading, searching, editing or building code and project files."),
          QStringLiteral("You are Noch's code specialist. Locate code with glob_grep before reading, view files before "
                         "editing, make the smallest correct change with exact str_replace edits, and verify with a "
                         "build or test command when one exists. Keep edits inside the workspace folder. You cannot "
                         "browse the web or control the computer.") },
    };
    return profiles;
}

const BuddyProvider::AgentProfile *BuddyProvider::profileFor(const QString &id)
{
    for (const AgentProfile &p : agentProfiles())
        if (p.id == id)
            return &p;
    return nullptr;
}

QString BuddyProvider::activeAgentTitle() const
{
    const AgentProfile *p = profileFor(m_activeAgent);
    return p ? p->title : QStringLiteral("Buddy");
}

void BuddyProvider::setHandoffs(bool on)
{
    if (on == m_handoffs)
        return;
    m_handoffs = on;
    emit agentConfigChanged();
}

void BuddyProvider::setActiveAgent(const QString &id)
{
    if (id == m_activeAgent)
        return;
    m_activeAgent = id;
    // OpenAI-format runs carry the system prompt as message 0: swap it.
    if (m_provider != QLatin1String("anthropic") && !m_agentMessages.isEmpty()) {
        QJsonObject sys = m_agentMessages.at(0).toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = systemPrompt();
            m_agentMessages[0] = sys;
        }
    }
    emit activeAgentChanged();
}

bool BuddyProvider::toolAllowedForActiveAgent(const QString &tool) const
{
    if (tool.startsWith(QLatin1String("transfer_to_")))
        return true;
    const AgentProfile *p = profileFor(m_activeAgent);
    return !p || p->tools.isEmpty() || p->tools.contains(tool);
}

void BuddyProvider::executeHandoff(const PendingToolCall &call)
{
    const QString target = call.name.mid(int(qstrlen("transfer_to_")));
    const AgentProfile *p = profileFor(target);
    if (!p || !m_handoffs || target == m_activeAgent) {
        finishCurrentToolCall(call, QStringLiteral("Error: no such agent to transfer to: '%1'.").arg(target), QString(), true);
        return;
    }
    const QString from = activeAgentTitle();
    ++m_handoffCount;
    setActiveAgent(target);
    addSpan(QJsonObject{ { "type", "handoff" }, { "from", from }, { "to", p->title },
                         { "reason", call.input.value(QStringLiteral("reason")).toString().left(200) } });
    QString msg = QStringLiteral("Transferred to %1. You are now %1.").arg(p->title);
    if (!p->instructions.isEmpty())
        msg += QLatin1Char(' ') + p->instructions;
    if (!p->tools.isEmpty())
        msg += QStringLiteral(" Tools available to you: %1.").arg(p->tools.join(QStringLiteral(", ")));
    finishCurrentToolCall(call, msg, QString(), false);
}

QString BuddyProvider::memoryIndex() const
{
    if (!m_allowMemory)
        return QString();
    const QString dir = memoriesPath();
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    QStringList entries;
    while (it.hasNext() && entries.size() < 16) {
        const QString f = it.next();
        const QFileInfo fi(f);
        entries << QStringLiteral("/memories/%1 (%2 B)").arg(QDir(dir).relativeFilePath(f)).arg(fi.size());
    }
    if (entries.isEmpty())
        return QStringLiteral("\n\nYour /memories directory is empty.");
    return QStringLiteral("\n\nSaved memories from earlier sessions (view the relevant ones before you start; "
                          "keep them up to date):\n- ") + entries.join(QStringLiteral("\n- "));
}

bool BuddyProvider::wantsStreaming() const
{
    // Ollama's native /api/chat streams NDJSON, not SSE; keep it buffered.
    return m_streamReplies && m_provider != QLatin1String("ollama")
        && m_provider != QLatin1String("none");
}

void BuddyProvider::setStreamText(const QString &text, bool live)
{
    const QString safe = text.isEmpty() ? QString() : redact(text);
    const bool changed = safe != m_streamText || live != m_streamingNow;
    m_streamText = safe;
    m_streamingNow = live;
    if (!changed)
        return;
    if (live && !safe.isEmpty()) {
        if (!m_streamEmitTimer.isActive())
            m_streamEmitTimer.start();   // coalesce token bursts
    } else {
        m_streamEmitTimer.stop();
        emit streamTextChanged();
    }
}

void BuddyProvider::onStreamReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_chatReply)
        return;
    if (!m_streamConsumed) {
        // Only parse as SSE when the server actually streamed; error bodies
        // and servers that ignore `stream` stay buffered for onReplyFinished.
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (status < 200 || status >= 300 || !ct.contains(QLatin1String("text/event-stream"), Qt::CaseInsensitive))
            return;
        m_streamConsumed = true;
        setActiveToolState(QString(), tr("Writing…"));
    }
    if (m_stream.feed(reply->readAll()))
        setStreamText(m_continuedText + m_stream.text(), true);
}

void BuddyProvider::setKeepTraces(bool on)
{
    if (on == m_keepTraces)
        return;
    m_keepTraces = on;
    emit agentConfigChanged();
}

QString BuddyProvider::effectiveWorkspace() const
{
    const QString r = m_workspaceRoot.trimmed();
    if (!r.isEmpty())
        return QDir::cleanPath(r);
    return QDir(QDir::homePath()).filePath(QStringLiteral("Noch Workspace"));
}

// Noch's own settings and data: the agent must not read the API key or raise
// its own permission mode by editing these folders.
void BuddyProvider::syncPrivatePaths()
{
    QStringList priv;
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (cfg.isEmpty())
        cfg = QDir::homePath() + QStringLiteral("/.config");
    priv << QDir(cfg).filePath(QStringLiteral("noch"));
    QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (data.isEmpty())
        data = QDir::homePath() + QStringLiteral("/.noch");
    priv << data;
    const QString appdata = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
    if (!appdata.isEmpty())
        priv << QDir(appdata).filePath(QStringLiteral("noch"));
    m_policy.setPrivatePaths(priv);
}

