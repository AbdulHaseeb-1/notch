#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

QString BuddyProvider::extractText(const QJsonDocument &doc) const
{
    const QJsonObject root = doc.object();
    if (m_provider == QLatin1String("anthropic")) {
        QStringList texts;
        const QJsonArray content = root[QStringLiteral("content")].toArray();
        for (const QJsonValue &v : content) {
            const QJsonObject block = v.toObject();
            const QString bType = block[QStringLiteral("type")].toString();
            if (bType == QLatin1String("text")) {
                const QString t = block[QStringLiteral("text")].toString().trimmed();
                if (!t.isEmpty())
                    texts << t;
            }
        }
        return texts.join(QStringLiteral("\n\n"));
    }
    if (m_provider == QLatin1String("ollama")) {
        const QJsonObject msg = root[QStringLiteral("message")].toObject();
        return msg[QStringLiteral("content")].toString();
    }
    const QJsonArray choices = root[QStringLiteral("choices")].toArray();
    if (!choices.isEmpty()) {
        const QJsonValue content = choices.first().toObject()[QStringLiteral("message")].toObject()[QStringLiteral("content")];
        if (content.isString())
            return content.toString();
        if (content.isArray()) {
            QString joined;
            const QJsonArray parts = content.toArray();
            for (const QJsonValue &part : parts)
                joined += part.toObject()[QStringLiteral("text")].toString();
            return joined;
        }
    }
    return QString();
}

// The answer text of one reply exactly as written (no trimming), so the
// parts of a continued answer join without eating the space between them.
QString BuddyProvider::rawAnswerText(const QJsonDocument &doc) const
{
    const QJsonObject root = doc.object();
    if (m_provider == QLatin1String("anthropic")) {
        QString out;
        for (const QJsonValue &v : root.value(QStringLiteral("content")).toArray()) {
            const QJsonObject block = v.toObject();
            if (block.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                out += block.value(QStringLiteral("text")).toString();
        }
        return out;
    }
    return extractText(doc);
}

QString BuddyProvider::errorText(const QByteArray &body, int status,
                                 const QString &fallback)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue err = root.value(QStringLiteral("error"));
        QString msg;
        if (err.isObject())
            msg = err.toObject().value(QStringLiteral("message")).toString().trimmed();
        else if (err.isString())
            msg = err.toString().trimmed();
        if (msg.isEmpty())
            msg = root.value(QStringLiteral("message")).toString().trimmed();
        if (!msg.isEmpty())
            return msg;
    }
    return tidyTransportError(fallback, status);
}

QStringList BuddyProvider::modelsIn(const QJsonDocument &doc, const QString &provider)
{
    QStringList ids;
    if (!doc.isObject())
        return ids;
    const QJsonObject root = doc.object();
    const QJsonArray rows = provider == QLatin1String("ollama")
        ? root.value(QStringLiteral("models")).toArray()
        : root.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &row : rows) {
        const QJsonObject o = row.toObject();
        const QString id = o.value(QStringLiteral("id")).toString().isEmpty()
            ? o.value(QStringLiteral("name")).toString()
            : o.value(QStringLiteral("id")).toString();
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !ids.contains(trimmed))
            ids << trimmed;
    }
    return ids;
}

void BuddyProvider::probe()
{
    if (m_provider == QLatin1String("none")) {
        emit testFinished(false, tr("No provider selected yet."));
        return;
    }
    if (needsApiKey() && m_apiKey.trimmed().isEmpty()) {
        emit testFinished(false, tr("No API key set."));
        return;
    }
    const QString url = modelsUrl();
    if (url.isEmpty()) {
        emit testFinished(false, tr("Set a base URL first."));
        return;
    }

    if (m_probeReply) {
        QNetworkReply *old = m_probeReply;
        m_probeReply = nullptr;
        old->abort();
    }
    m_probeReply = m_nam.get(buildRequest(url));
}

void BuddyProvider::sendMessage(const QString &text, const QVariantList &context, bool spoken)
{
    if (m_provider == QLatin1String("none") || !m_enabled) {
        emit errorOccurred(QStringLiteral("No chat provider configured. Open Settings > Buddy to add one."));
        return;
    }
    if (needsApiKey() && m_apiKey.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("No API key set. Open Settings > Buddy to add one."));
        return;
    }
    if (text.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Nothing to send."));
        return;
    }
    if (m_model.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("No model selected. Open Settings > Buddy to pick one."));
        return;
    }

    cancel();
    m_spokenRun = spoken;   // after cancel(): a new run decides, a stale one must not leak in
    clearToolSteps();
    m_policy.setTainted(false);
    m_policy.setScreenTainted(false);
    m_computer->rateLimiter().reset();
    m_turnTimer.start();
    m_retryCount = 0;
    m_traceSpans = QJsonArray();
    m_traceId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))
              + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate() & 0xffff, 16);
    addSpan(QJsonObject{ { "type", "run" }, { "event", "start" }, { "chars", text.size() },
                         { "historyTurns", context.size() / 2 } });
    beginJournalRun();
    m_agentIteration = 0;
    m_toolsDisabledForTurn = false;
    m_emptyToolIdInTurn = false;
    m_accumulatedText.clear();
    m_continuedText.clear();
    m_continuations = 0;
    m_truncated = false;
    m_handoffCount = 0;
    if (m_classification.valid) {
        m_classification = turnclass::Classification();
        emit lastClassificationChanged();
    }
    m_classifyPending = false;
    m_computerBatchFailed = false;
    m_lastCacheMark = -1;
    // One memory listing per run: a stable system prompt keeps the prompt
    // cache warm even when the model writes memories mid-run. P8: ranked by
    // relevance to this run's request.
    m_memoryIndexSnapshot = memoryIndex(text);
    {
        // Instruction files are read once per run for the same reason: an edit
        // mid-run must not change the system prompt and spoil the cache.
        const pc::Instructions ins = pc::loadInstructions(effectiveWorkspace(), userDataDir());
        m_instructionsSection = pc::systemPromptSection(ins);
        if (!ins.empty()) {
            QJsonArray names;
            for (const pc::InstructionSource &s : ins.sources)
                names.append(s.name);
            addSpan(QJsonObject{ { "type", "instructions" }, { "sources", names },
                                 { "chars", ins.text.size() }, { "truncated", ins.truncated } });
        }
    }
    if (m_activeAgent != QLatin1String("buddy")) {   // every run starts with Buddy
        m_activeAgent = QStringLiteral("buddy");
        emit activeAgentChanged();
    }
    m_agentMessages = QJsonArray();
    m_pendingTools.clear();
    m_completedTools.clear();

    if (m_provider != QLatin1String("anthropic")) {
        m_agentMessages.append(QJsonObject{
            { QStringLiteral("role"), QStringLiteral("system") },
            { QStringLiteral("content"), systemPrompt() }
        });
    }

    // Session memory: keep the newest turns verbatim inside a character
    // budget; older turns collapse into a one-line rolling summary of what
    // was asked, so long sessions keep their thread without the token cost.
    constexpr int kHistoryBudgetChars = 24000;
    // A real summary (from ChatSessions) opens the context as its own pair.
    // It is always sent and never folded into the one-line note below.
    const bool hasSummary = context.size() >= 2
        && context.at(0).toMap().value(QStringLiteral("content")).toString().startsWith(QLatin1String(kConversationSummaryPrefix))
        && context.at(0).toMap().value(QStringLiteral("role")).toString() == QLatin1String("user");
    const int base = hasSummary ? 2 : 0;
    if (hasSummary) {
        m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") },
            { QStringLiteral("content"), shortenForHistory(context.at(0).toMap().value(QStringLiteral("content")).toString()) } });
        m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") },
            { QStringLiteral("content"), context.at(1).toMap().value(QStringLiteral("content")).toString().isEmpty()
                  ? QStringLiteral("Understood.") : context.at(1).toMap().value(QStringLiteral("content")).toString() } });
        addSpan(QJsonObject{ { "type", "session_summary" }, { "chars", context.at(0).toMap().value(QStringLiteral("content")).toString().size() } });
    }
    int firstKept = base;
    {
        int used = 0;
        for (int i = (context.size() & ~1) - 2; i >= base; i -= 2) {
            const int len = qMin(kHistoryMsgChars, int(context.at(i).toMap().value(QStringLiteral("content")).toString().size()))
                          + qMin(kHistoryMsgChars, int(context.at(i + 1).toMap().value(QStringLiteral("content")).toString().size()));
            if (used + len > kHistoryBudgetChars) {
                firstKept = i + 2;
                break;
            }
            used += len;
        }
    }
    if (firstKept > base) {
        QStringList asked;
        for (int i = base; i < firstKept; i += 2) {
            QString q = context.at(i).toMap().value(QStringLiteral("content")).toString().simplified();
            if (q.size() > 90)
                q = q.left(87) + QStringLiteral("…");
            if (!q.isEmpty())
                asked << q;
        }
        if (asked.size() > 12)
            asked = asked.mid(asked.size() - 12);
        if (!asked.isEmpty()) {
            m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") },
                { QStringLiteral("content"), QStringLiteral("[Session memory] Earlier in this session I asked about: ")
                                             + asked.join(QStringLiteral(" · ")) } });
            m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") },
                { QStringLiteral("content"), QStringLiteral("Noted — I remember those earlier topics.") } });
        }
        addSpan(QJsonObject{ { "type", "session_memory" }, { "summarisedTurns", (firstKept - base) / 2 } });
    }

    for (int i = firstKept; i + 1 < context.size(); i += 2) {
        const QVariantMap q = context.at(i).toMap();
        const QVariantMap a = context.at(i + 1).toMap();
        const QString qText = shortenForHistory(q.value(QStringLiteral("content")).toString());
        const QString aText = shortenForHistory(a.value(QStringLiteral("content")).toString());
        if (q.value(QStringLiteral("role")).toString() != QLatin1String("user")
                || a.value(QStringLiteral("role")).toString() != QLatin1String("assistant")
                || qText.trimmed().isEmpty() || aText.trimmed().isEmpty())
            continue;
        m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("user") }, { QStringLiteral("content"), qText } });
        m_agentMessages.append(QJsonObject{ { QStringLiteral("role"), QStringLiteral("assistant") }, { QStringLiteral("content"), aText } });
    }

    m_agentMessages.append(QJsonObject{
        { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("content"), text }
    });

    emit statusChanged(QStringLiteral("thinking"));
    dispatchAfterClassify(text);
}

void BuddyProvider::dispatchAgentTurn()
{
    const int cleared = compactContext();
    if (cleared > 0)
        addSpan(QJsonObject{ { "type", "compaction" }, { "clearedToolResults", cleared } });
    m_modelTimer.start();
    const QString url = buildUrl();
    QNetworkRequest request = buildRequest(url);
    // Qt's transfer timeout is an inactivity timeout. A streamed reply sends
    // bytes (or pings) all the time; a buffered one is silent until the
    // whole answer is ready, so 90 s used to abort long answers.
    request.setTransferTimeout(wantsStreaming() ? kStreamIdleTimeoutMs : kBufferedReplyTimeoutMs);
    const QJsonObject payload = buildAgentPayload();
    addBetaHeaders(request, payload);
    QJsonDocument doc(payload);
    m_streamConsumed = false;
    m_stream.reset(m_provider == QLatin1String("anthropic") ? StreamAccumulator::Format::Anthropic
                                                           : StreamAccumulator::Format::OpenAI);
    // A continuation keeps showing the text written so far.
    setStreamText(m_continuedText, false);
    m_chatReply = m_nam.post(request, doc.toJson(QJsonDocument::Compact));
    if (wantsStreaming())
        connect(m_chatReply, &QNetworkReply::readyRead, this, &BuddyProvider::onStreamReadyRead);
}

void BuddyProvider::runDirectCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Nothing to run."));
        return;
    }

    cancel();
    clearToolSteps();
    beginJournalRun();
    m_turnTimer.start();
    m_agentIteration = -1; // Direct tool execution marker (no LLM follow-up turn)
    m_accumulatedText.clear();
    m_pendingTools.clear();
    m_completedTools.clear();

    PendingToolCall tc;
    tc.id = QStringLiteral("direct_bash_1");
    tc.name = QStringLiteral("bash");
    tc.input = QJsonObject{ { QStringLiteral("command"), trimmed } };

    QVariantMap step;
    step[QStringLiteral("id")] = tc.id;
    step[QStringLiteral("index")] = 1;
    step[QStringLiteral("tool")] = tc.name;
    step[QStringLiteral("badge")] = badgeForTool(tc.name, tc.input);
    step[QStringLiteral("icon")] = iconForTool(tc.name, tc.input);
    step[QStringLiteral("title")] = summarizeToolCall(tc.name, tc.input);
    step[QStringLiteral("detail")] = detailForToolCall(tc.name, tc.input);
    step[QStringLiteral("output")] = QString();
    step[QStringLiteral("status")] = QStringLiteral("running");
    step[QStringLiteral("durationMs")] = 0;
    tc.stepIndex = 0;
    m_toolSteps.append(step);
    m_pendingTools.append(tc);

    emit statusChanged(QStringLiteral("thinking"));
    emit toolStepsChanged();
    processNextToolCall();
}

void BuddyProvider::runDirectWebSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Nothing to search."));
        return;
    }

    cancel();
    clearToolSteps();
    m_turnTimer.start();
    m_agentIteration = -1; // Direct tool execution marker
    m_accumulatedText.clear();
    m_pendingTools.clear();
    m_completedTools.clear();

    PendingToolCall tc;
    tc.id = QStringLiteral("direct_search_1");
    if (trimmed.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || trimmed.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        tc.name = QStringLiteral("web_fetch");
        tc.input = QJsonObject{
            { QStringLiteral("url"), trimmed },
            { QStringLiteral("extract_mode"), QStringLiteral("text") }
        };
    } else {
        tc.name = QStringLiteral("web_search");
        tc.input = QJsonObject{ { QStringLiteral("query"), trimmed } };
    }

    QVariantMap step;
    step[QStringLiteral("id")] = tc.id;
    step[QStringLiteral("index")] = 1;
    step[QStringLiteral("tool")] = tc.name;
    step[QStringLiteral("badge")] = badgeForTool(tc.name, tc.input);
    step[QStringLiteral("icon")] = iconForTool(tc.name, tc.input);
    step[QStringLiteral("title")] = summarizeToolCall(tc.name, tc.input);
    step[QStringLiteral("detail")] = detailForToolCall(tc.name, tc.input);
    step[QStringLiteral("output")] = QString();
    step[QStringLiteral("status")] = QStringLiteral("running");
    step[QStringLiteral("durationMs")] = 0;
    tc.stepIndex = 0;
    m_toolSteps.append(step);
    m_pendingTools.append(tc);

    emit statusChanged(QStringLiteral("thinking"));
    emit toolStepsChanged();
    processNextToolCall();
}

void BuddyProvider::runDirectToolSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Nothing to search."));
        return;
    }

    cancel();
    clearToolSteps();
    m_turnTimer.start();
    m_agentIteration = -1;
    m_accumulatedText.clear();
    m_pendingTools.clear();
    m_completedTools.clear();

    PendingToolCall tc;
    tc.id = QStringLiteral("direct_tool_search_1");
    tc.name = QStringLiteral("tool_search");
    const bool looksRegex = trimmed.contains('^') || trimmed.contains('$') || trimmed.contains('*') || trimmed.contains('|');
    tc.input = QJsonObject{
        { QStringLiteral("query"), trimmed },
        { QStringLiteral("mode"), looksRegex ? QStringLiteral("regex") : QStringLiteral("bm25") }
    };

    QVariantMap step;
    step[QStringLiteral("id")] = tc.id;
    step[QStringLiteral("index")] = 1;
    step[QStringLiteral("tool")] = tc.name;
    step[QStringLiteral("badge")] = badgeForTool(tc.name, tc.input);
    step[QStringLiteral("icon")] = iconForTool(tc.name, tc.input);
    step[QStringLiteral("title")] = summarizeToolCall(tc.name, tc.input);
    step[QStringLiteral("detail")] = detailForToolCall(tc.name, tc.input);
    step[QStringLiteral("output")] = QString();
    step[QStringLiteral("status")] = QStringLiteral("running");
    step[QStringLiteral("durationMs")] = 0;
    tc.stepIndex = 0;
    m_toolSteps.append(step);
    m_pendingTools.append(tc);

    emit statusChanged(QStringLiteral("thinking"));
    emit toolStepsChanged();
    processNextToolCall();
}

void BuddyProvider::runDirectAdvisor(const QString &question)
{
    const QString trimmed = question.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred(tr("Nothing to ask the Advisor."));
        return;
    }

    cancel();
    clearToolSteps();
    m_turnTimer.start();
    m_agentIteration = -1;
    m_accumulatedText.clear();
    m_pendingTools.clear();
    m_completedTools.clear();

    PendingToolCall tc;
    tc.id = QStringLiteral("direct_advisor_1");
    tc.name = QStringLiteral("advisor");
    tc.input = QJsonObject{
        { QStringLiteral("task"), trimmed },
        { QStringLiteral("question"), trimmed },
        { QStringLiteral("focus"), QStringLiteral("architecture") }
    };

    QVariantMap step;
    step[QStringLiteral("id")] = tc.id;
    step[QStringLiteral("index")] = 1;
    step[QStringLiteral("tool")] = tc.name;
    step[QStringLiteral("badge")] = badgeForTool(tc.name, tc.input);
    step[QStringLiteral("icon")] = iconForTool(tc.name, tc.input);
    step[QStringLiteral("title")] = summarizeToolCall(tc.name, tc.input);
    step[QStringLiteral("detail")] = detailForToolCall(tc.name, tc.input);
    step[QStringLiteral("output")] = QString();
    step[QStringLiteral("status")] = QStringLiteral("running");
    step[QStringLiteral("durationMs")] = 0;
    tc.stepIndex = 0;
    m_toolSteps.append(step);
    m_pendingTools.append(tc);

    emit statusChanged(QStringLiteral("thinking"));
    emit toolStepsChanged();
    processNextToolCall();
}

void BuddyProvider::cancel()
{
    ++m_runGeneration;
    m_classifyPending = false;
    m_classifier->cancel();
    if (m_classification.valid) {
        m_classification = turnclass::Classification();
        emit lastClassificationChanged();
    }
    m_pendingTools.clear();
    m_completedTools.clear();
    m_retryTimer.stop();
    m_policy.setTainted(false);
    clearPendingApproval();
    clearPendingQuestion();
    m_streamConsumed = false;
    m_continuedText.clear();
    setStreamText(QString(), false);
    if (m_activeAgent != QLatin1String("buddy")) {   // no specialist outlives its run
        m_activeAgent = QStringLiteral("buddy");
        emit activeAgentChanged();
    }
    finishTrace(QStringLiteral("cancelled"));
    if (m_activeProcess) {
        QProcess *proc = m_activeProcess;
        m_activeProcess = nullptr;
        proc->disconnect(this);
        proc->kill();
        proc->deleteLater();
    }
    if (m_webSearchReply) {
        QNetworkReply *old = m_webSearchReply;
        m_webSearchReply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    if (m_advisorReply) {
        QNetworkReply *old = m_advisorReply;
        m_advisorReply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    if (m_chatReply) {
        QNetworkReply *old = m_chatReply;
        m_chatReply = nullptr;
        old->abort();
    }
    setActiveToolState(QString(), QString());
    emit statusChanged(QStringLiteral("idle"));
}

void BuddyProvider::finishProbe(bool transportOk, int status, const QByteArray &body,
                                const QString &fallback)
{
    if (!transportOk || status < 200 || status >= 300) {
        emit testFinished(false, errorText(body, status, fallback));
        return;
    }
    const QStringList models = modelsIn(QJsonDocument::fromJson(body), m_provider);
    emit testFinished(true, models.isEmpty()
        ? tr("Connected.")
        : tr("Connected - %n model(s) available", "", models.size()));
    emit modelsFetched(models);
}

