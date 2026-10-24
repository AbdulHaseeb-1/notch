#include "BuddyProviderInternal.h"
#include "ToolRegistry.h"
#include "MemoryStore.h"
#include "AppStorage.h"

using namespace noch::buddyprovider;

BuddyProvider::BuddyProvider(QObject *parent)
    : QObject(parent)
{
    connect(&m_nam, &QNetworkAccessManager::finished,
            this, &BuddyProvider::onReplyFinished);

    // Long-term memory: its own network client, so the chat provider's
    // reply handling never sees its traffic. The client keeps owning the
    // transport and the settings surface; both stores are reached through
    // the MemoryBackend interface, and the client's searched/added
    // signals are consumed by the Mem0 backend, not here.
    m_mem = new Mem0Client(this);
    connect(m_mem, &Mem0Client::testFinished, this, &BuddyProvider::memTestFinished);
    m_mem->setProviderSupplier([this] { return memProviderJson(); });
    m_mem0Backend = new Mem0MemoryBackend(m_mem, this);
    m_localBackend.setRootSupplier([this] { return memoriesPath(); });
    m_localBackend.setSanitizer([this](const QString &text) { return redact(text); });

    // Connected apps. Nothing is read from disk until startConnectors() (tests never touch real files).
    m_connectors = new ConnectorRegistry(this);
    connect(m_connectors, &ConnectorRegistry::toolsChanged, this, &BuddyProvider::syncConnectorPolicy);
    connect(m_connectors, &ConnectorRegistry::toolsChanged, this, &BuddyProvider::agentConfigChanged);

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
    m_memRaceTimer.setSingleShot(true);
    connect(&m_memRaceTimer, &QTimer::timeout, this, &BuddyProvider::onMemRecallRaceTimeout);
    m_policy.setWorkspaceRoots({ effectiveWorkspace() });
    syncPrivatePaths();
    registerBuiltinHooks();
}

// P3: the harness's lifecycle points are hooks on the bus. Registration
// order is significant: PreToolUse rewrites (path-normalize) run before the
// policy hook so the permission decision sees canonical paths.
void BuddyProvider::registerBuiltinHooks()
{
    // Input rewriting: relative editor paths and shell working dirs resolve
    // against the workspace before anything judges them.
    m_hookBus.addHook(HookEvent::PreToolUse, QStringLiteral("path-normalize"),
        [this](const HookContext &ctx) -> HookResult {
            QJsonObject in = ctx.input;
            bool changed = false;
            const QString base = effectiveWorkspace();
            // P2: which argument is a workspace-relative path is
            // registry metadata (bash: working_dir, editor: path).
            if (const CoreToolDef *def = coreToolDef(ctx.toolName)) {
                if (!def->workspacePathArg.isEmpty()) {
                    const QString raw = in.value(def->workspacePathArg).toString();
                    if (!raw.isEmpty() && QDir::isRelativePath(raw)) {
                        in[def->workspacePathArg] = QDir(base).absoluteFilePath(raw);
                        changed = true;
                    }
                }
            }
            return changed ? HookResult::rewriteInput(in) : HookResult::allow();
        });

    // The permission policy reframed as a PreToolUse hook. The full Decision
    // is stashed for the gate's bookkeeping (spans, grants, approval UI).
    m_hookBus.addHook(HookEvent::PreToolUse, QStringLiteral("policy"),
        [this](const HookContext &ctx) -> HookResult {
            m_lastPolicyDecision = m_policy.evaluate(ctx.toolName, ctx.input);
            switch (m_lastPolicyDecision.verdict) {
            case AgentPolicy::Verdict::Deny:
                return HookResult::deny(m_lastPolicyDecision.rule, m_lastPolicyDecision.reason);
            case AgentPolicy::Verdict::Ask:
                return HookResult::ask(m_lastPolicyDecision.rule, m_lastPolicyDecision.reason);
            case AgentPolicy::Verdict::Allow:
                break;
            }
            return HookResult::allow();
        });

    // Tool output guardrail as a PostToolUse hook: secret redaction,
    // untrusted-content fencing and injection detection on every output
    // (error outputs included — the call site always runs the bus).
    m_hookBus.addHook(HookEvent::PostToolUse, QStringLiteral("output-guard"),
        [this](const HookContext &ctx) -> HookResult {
            PendingToolCall probe;
            probe.name = ctx.toolName;
            probe.input = ctx.input;
            bool flagged = false;
            const QString guarded = guardToolOutput(probe, ctx.output, &flagged);
            m_outputGuardFlagged = flagged;
            return guarded != ctx.output ? HookResult::rewriteOutput(guarded)
                                          : HookResult::allow();
        });
}

// ---- long-term memory (Mem0) -------------------------------------------------

void BuddyProvider::setMemEnabled(bool on)
{
    if (on == m_mem->enabled())
        return;
    m_mem->setEnabled(on);
    if (!on) {
        m_memPending = false;
        m_memLatePending = false;
        m_memRaceTimer.stop();
    }
    emit memConfigChanged();
}

void BuddyProvider::setMemBackend(const QString &backend)
{
    const QString before = m_mem->backend();
    m_mem->setConfig(backend, m_mem->customUrl(), m_memKeyTyped, m_mem->userId());
    if (m_mem->backend() != before)
        emit memConfigChanged();
}

void BuddyProvider::setMemUrl(const QString &url)
{
    const QString before = m_mem->customUrl();
    m_mem->setConfig(m_mem->backend(), url, m_memKeyTyped, m_mem->userId());
    if (m_mem->customUrl() != before)
        emit memConfigChanged();
}

void BuddyProvider::setMemKey(const QString &key)
{
    const QString k = key.trimmed();
    if (k == m_memKeyTyped)
        return;
    m_memKeyTyped = k;
    m_mem->setConfig(m_mem->backend(), m_mem->customUrl(), k, m_mem->userId());
    emit memConfigChanged();
}

void BuddyProvider::setMemUserId(const QString &id)
{
    const QString before = m_mem->userId();
    m_mem->setConfig(m_mem->backend(), m_mem->customUrl(), m_memKeyTyped, id);
    if (m_mem->userId() != before)
        emit memConfigChanged();
}

#define NOCH_MEM_STRING_SETTER(Setter, Member)            \
    void BuddyProvider::Setter(const QString &v)          \
    {                                                     \
        const QString t = v.trimmed();                    \
        if (t == Member)                                  \
            return;                                       \
        Member = t;                                       \
        emit memConfigChanged();                          \
    }
NOCH_MEM_STRING_SETTER(setMemLlmModel, m_memLlmModel)
NOCH_MEM_STRING_SETTER(setMemEmbedModel, m_memEmbedModel)
NOCH_MEM_STRING_SETTER(setMemEmbedBase, m_memEmbedBase)
NOCH_MEM_STRING_SETTER(setMemEmbedKey, m_memEmbedKey)
#undef NOCH_MEM_STRING_SETTER

namespace {
// The local Mem0 server runs in a container: this computer is host.docker.internal there.
QString forContainer(const QString &base)
{
    QUrl url(base);
    const QString host = url.host().toLower();
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")) {
        url.setHost(QStringLiteral("host.docker.internal"));
        return url.toString();
    }
    return base;
}

// OpenAI-style clients want the address that ends in /v1.
QString openAiBase(const QString &base)
{
    QString b = base.trimmed();
    while (b.endsWith(QLatin1Char('/')))
        b.chop(1);
    for (const char *tail : { "/chat/completions", "/embeddings", "/models" })
        if (b.endsWith(QLatin1String(tail)))
            b.chop(int(qstrlen(tail)));
    static const QRegularExpression versioned(QStringLiteral("/v\\d+$"));
    if (!versioned.match(b).hasMatch())
        b += QStringLiteral("/v1");
    return b;
}
} // namespace

QJsonObject BuddyProvider::memProviderJson() const
{
    if (profile().dialect == provider::Dialect::None
            || profile().dialect == provider::Dialect::Anthropic)
        return {};
    const QString rawBase = m_apiBase.trimmed().isEmpty() ? profile().defaultBase : m_apiBase;
    if (rawBase.trimmed().isEmpty())
        return {};
    const bool ollama = profile().dialect == provider::Dialect::Ollama;
    const QString base = openAiBase(forContainer(rawBase));
    const QString key = !m_apiKey.trimmed().isEmpty() ? m_apiKey.trimmed()
                      : ollama ? QStringLiteral("ollama") : QString();

    QJsonObject llm{ { QStringLiteral("base_url"), base },
                     { QStringLiteral("api_key"), key },
                     { QStringLiteral("model"), m_memLlmModel.isEmpty() ? m_model : m_memLlmModel } };
    QJsonObject emb{ { QStringLiteral("base_url"), m_memEmbedBase.isEmpty() ? base : openAiBase(forContainer(m_memEmbedBase)) },
                     { QStringLiteral("api_key"), m_memEmbedKey.isEmpty() ? key : m_memEmbedKey },
                     { QStringLiteral("model"), !m_memEmbedModel.isEmpty() ? m_memEmbedModel
                           : ollama ? QStringLiteral("nomic-embed-text") : QStringLiteral("text-embedding-3-small") } };
    return QJsonObject{ { QStringLiteral("llm"), llm }, { QStringLiteral("embedder"), emb } };
}

// The first model turn of a run gives recall a short head start (kMemRecallRaceMs),
// so what Mem0 knows about the user is usually already in the system prompt —
// but a slow or cold memory backend must not hold the run hostage: when the race
// timer wins, the turn dispatches with the local snapshot only and the search
// stays in flight; its hits still land in the prompt from the next step onward.
// Anything that goes wrong just dispatches without it.
void BuddyProvider::recallThenDispatch(const QString &text)
{
    if (!m_allowMemory || !m_mem0Backend->recallReady() || text.trimmed().size() < 3) {
        dispatchAgentTurn();
        return;
    }
    m_memPending = true;
    m_memLatePending = false;
    const QPointer<BuddyProvider> self(this);
    m_mem0Backend->recall(text, 6, m_runGeneration,
        [self](quint64 token, const QList<MemoryHit> &hits, const QString &error, int ms) {
            if (self)
                self->onRecallCompleted(token, hits, error, ms);
        });
    m_memRaceTimer.start(kMemRecallRaceMs);
}

void BuddyProvider::onMemRecallRaceTimeout()
{
    if (!m_memPending)
        return;   // recall landed first (or the run was cancelled meanwhile)
    m_memPending = false;
    m_memLatePending = true;   // the search is still out; its hits are welcome late
    dispatchAgentTurn();
}

void BuddyProvider::onRecallCompleted(quint64 token, const QList<MemoryHit> &hits, const QString &error, int ms)
{
    if (token != m_runGeneration)
        return;   // cancelled or superseded while the request was in flight

    // Two ways to get here: recall beat the race (m_memPending — the turn is
    // still waiting, dispatch below), or it lost (m_memLatePending — the turn
    // already dispatched; fold the hits into the prompt for the next step and
    // never dispatch again). Neither flag set means the run ended meanwhile
    // (finishTrace clears m_memLatePending): drop the hits silently.
    const bool late = m_memLatePending;
    if (!m_memPending && !late)
        return;
    m_memPending = false;
    m_memLatePending = false;
    m_memRaceTimer.stop();

    QJsonObject span{ { QStringLiteral("type"), QStringLiteral("memory") },
                      { QStringLiteral("event"), QStringLiteral("recall") },
                      { QStringLiteral("backend"), m_mem->backend() },
                      { QStringLiteral("ms"), ms },
                      { QStringLiteral("hits"), int(hits.size()) } };
    if (late)
        span[QStringLiteral("late")] = true;
    if (!error.isEmpty())
        span[QStringLiteral("error")] = error;
    addSpan(span);

    if (!hits.isEmpty()) {
        // The one merge point: the snapshot's local hits and the recall
        // hits are deduped, ranked and capped, then split back by source
        // for rendering (the local lines keep their /memories paths, the
        // recalled ones their fenced block).
        const QList<MemoryHit> merged = mergeMemoryHits(m_snapshotLocalHits, hits, kSnapshotMemoryCap);
        QList<MemoryHit> local, remote;
        for (const MemoryHit &h : merged)
            (h.backend == QLatin1String("mem0") ? remote : local).append(h);
        m_snapshotLocalHits = local;
        m_memoryIndexSnapshot = renderLocalMemorySection(local) + redact(memoryPromptSection(remote));
        // The system prompt was built before recall: rebuild it with the new section.
        // (Anthropic builds it per request, so it picks the snapshot up by itself.)
        if (!m_agentMessages.isEmpty()) {
            QJsonObject first = m_agentMessages.at(0).toObject();
            if (first.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
                first[QStringLiteral("content")] = systemPrompt();
                m_agentMessages[0] = first;
            }
        }
    }
    if (!late)
        dispatchAgentTurn();
}

void BuddyProvider::onToolSearchCompleted(quint64 token, const QList<MemoryHit> &hits, const QString &error)
{
    if (token != m_runGeneration)
        return;   // cancelled or superseded while the request was in flight
    if (!m_memToolPending)
        return;   // the tool call already finished or was superseded
    m_memToolPending = false;
    finishMemorySearch(m_memToolCall, m_memToolQuery, m_memToolLocalHits, hits, error);
}

// Saving is fire-and-forget: the extraction runs a model and may take a while.
void BuddyProvider::saveExchangeToMem0(const QString &finalText, const QStringList &fallbackFacts)
{
    const QString user = m_runUserText.trimmed();
    // Slash-command expansions and harness turns are not the user's voice, and a bare
    // "ok" has nothing to remember.
    if (user.size() < 12 || user.startsWith(QLatin1Char('[')))
        return;
    constexpr int kUserChars = 2000;
    constexpr int kReplyChars = 1200;
    QList<MemoryMessage> msgs;
    msgs.append({ QStringLiteral("user"), redact(user.left(kUserChars)) });
    const QString reply = finalText.trimmed();
    if (!reply.isEmpty())
        msgs.append({ QStringLiteral("assistant"), redact(reply.left(kReplyChars)) });
    const quint64 seq = ++m_memAddSeq;
    if (!fallbackFacts.isEmpty())
        m_memFallback.insert(seq, fallbackFacts);
    const QPointer<BuddyProvider> self(this);
    m_mem0Backend->addExchange(msgs, seq, QJsonObject{ { QStringLiteral("source"), QStringLiteral("noch") } },
        [self](quint64 token, bool ok, const QString &error) {
            if (self)
                self->onExchangeSaved(token, ok, error);
        });
    addSpan(QJsonObject{ { QStringLiteral("type"), QStringLiteral("memory") },
                         { QStringLiteral("event"), QStringLiteral("save-sent") },
                         { QStringLiteral("backend"), m_mem->backend() } });
}

void BuddyProvider::onExchangeSaved(quint64 token, bool ok, const QString &error)
{
    const QStringList fallback = m_memFallback.take(token);
    if (ok)
        return;
    qWarning().noquote() << "Mem0: could not save the exchange:" << error;
    // Mem0 is down or refused: keep the explicit "remember that ..." facts locally,
    // so a stopped container never costs the user something they asked to be kept.
    if (!fallback.isEmpty() && m_allowMemory)
        storeLocalFacts(fallback, false);
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

// Model-agnostic harness: whether the configured model can process images.
// Anthropic models can; elsewhere we go by model name, because the chat APIs
// do not expose the capability. Unknown models count as blind: an image
// payload to a text-only model fails the whole turn, while a blind model
// only gets a note that the screenshot was withheld.
bool BuddyProvider::modelVisionCapable() const
{
    if (profile().supportsVision)
        return true;
    const QString m = m_model.toLower();
    static const QStringList hints{
        QStringLiteral("gpt-4o"), QStringLiteral("gpt-4-turbo"),
        QStringLiteral("gpt-4-vision"), QStringLiteral("o1"), QStringLiteral("o3"),
        QStringLiteral("claude"), QStringLiteral("gemini"),
        QStringLiteral("vision"), QStringLiteral("llava"),
        QStringLiteral("qwen-vl"), QStringLiteral("qwen2-vl"),
        QStringLiteral("minicpm-v"), QStringLiteral("pixtral"),
        QStringLiteral("moondream"), QStringLiteral("bakllava"),
        QStringLiteral("llama-3.2"), QStringLiteral("molmo"),
        QStringLiteral("gpt-4.1"), QStringLiteral("gpt-4.5"), QStringLiteral("gpt-5"),
        QStringLiteral("o4"), QStringLiteral("gemma3"), QStringLiteral("gemma-3"),
        QStringLiteral("qwen2.5vl"), QStringLiteral("qwen2.5-vl"), QStringLiteral("qwen3-vl"),
        QStringLiteral("llama4"), QStringLiteral("llama-4"), QStringLiteral("mistral-small3"),
        QStringLiteral("internvl"), QStringLiteral("glm-4v"),
    };
    for (const QString &h : hints) {
        if (m.contains(h))
            return true;
    }
    return false;
}

// P2: which rung of the structured-output ladder does this provider+model
// get? Native function calling where it's supported; Ollama models without
// tool support fall to JSON mode (constrained by the envelope schema);
// anything else gets the text parser.
BuddyProvider::StructuredLevel BuddyProvider::structuredLevel() const
{
    if (profile().dialect != provider::Dialect::Ollama)
        return StructuredLevel::FunctionCalling;
    const QString m = m_model.toLower();
    static const QStringList toolCapable{
        QStringLiteral("llama3.1"), QStringLiteral("llama3.2"), QStringLiteral("llama3.3"),
        QStringLiteral("llama4"), QStringLiteral("qwen2.5"), QStringLiteral("qwen3"),
        QStringLiteral("mistral"), QStringLiteral("mixtral"), QStringLiteral("command-r"),
        QStringLiteral("firefunction"), QStringLiteral("hermes"), QStringLiteral("devstral"),
        QStringLiteral("magistral"), QStringLiteral("gpt-oss"),
    };
    for (const QString &h : toolCapable)
        if (m.contains(h))
            return StructuredLevel::FunctionCalling;
    return StructuredLevel::JsonMode;
}

QJsonObject BuddyProvider::textProtocolSchema() const
{
    // {"text": "...", "tool_calls": [{"name": "bash", "input": {...}}]}
    QJsonObject callSchema{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("name"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
            { QStringLiteral("input"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("object") } } } } },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("name"), QStringLiteral("input") } },
    };
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("text"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
            { QStringLiteral("tool_calls"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("array") },
                { QStringLiteral("items"), callSchema } } } } },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("tool_calls") } },
    };
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
    if (profile().dialect != provider::Dialect::Anthropic || !m_officialComputer)
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
    if (profile().dialect != provider::Dialect::Anthropic)
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

void BuddyProvider::setShowThinking(bool on)
{
    if (on == m_showThinking)
        return;
    m_showThinking = on;
    emit agentConfigChanged();
}

// P8: the per-run memory snapshot is now top-K by relevance to the
// current request (BM25 over /memories) with one-line gists, not a raw
// file listing. Stable per run so the prompt cache stays warm; the model
// can pull full files via memory_search/memory. The hits are kept
// structured (m_snapshotLocalHits) so recall can re-merge them with its
// remote hits through mergeMemoryHits instead of string-appending.
QString BuddyProvider::memoryIndex(const QString &query)
{
    if (!m_allowMemory) {
        m_snapshotLocalHits.clear();
        m_snapshotLocalFallback = false;
        m_snapshotCorpusEmpty = false;
        return QString();
    }
    // Routed through the one merge point too (dedupe within the local
    // results); the backend already caps at 8 hits / 4 newest.
    m_snapshotLocalHits = mergeMemoryHits(
        m_localBackend.snapshotHits(query, &m_snapshotCorpusEmpty, &m_snapshotLocalFallback), {}, 8);
    return renderLocalMemorySection(m_snapshotLocalHits);
}

// The local half of the prompt snapshot, from structured hits. The
// "newest" fallback lines carry no [procedure] tag, exactly as before.
QString BuddyProvider::renderLocalMemorySection(const QList<MemoryHit> &hits) const
{
    if (m_snapshotCorpusEmpty)
        return QStringLiteral("\n\nYour /memories directory is empty.");
    if (hits.isEmpty())
        return QString();
    QStringList entries;
    for (const MemoryHit &h : hits) {
        entries << (m_snapshotLocalFallback
            ? QStringLiteral("- /memories/%1 — %2").arg(h.id, h.text)
            : QStringLiteral("- /memories/%1%2 — %3")
                       .arg(h.id,
                            h.procedure ? QStringLiteral(" [procedure]") : QString(),
                            h.text));
    }
    if (m_snapshotLocalFallback)
        return QStringLiteral("\n\nSaved memories (newest; use memory_search to recall by topic):\n")
             + entries.join(QChar('\n'));
    return QStringLiteral("\n\nMemories relevant to this request (use memory_search to dig deeper, "
                          "and keep them up to date):\n") + entries.join(QChar('\n'));
}

bool BuddyProvider::wantsStreaming() const
{
    // Ollama's native /api/chat streams NDJSON, not SSE; keep it buffered.
    // (The supportsStreaming flag is false exactly for ollama and none.)
    return m_streamReplies && profile().supportsStreaming;
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
    // The vault and the settings file, wherever install() resolved them.
    priv << noch::storage::dataDir();
    priv << noch::storage::connectorsDir();
    m_policy.setPrivatePaths(priv);
}

