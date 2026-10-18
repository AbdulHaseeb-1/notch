#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "AgentPolicy.h"

// Backend that connects Noch's desktop buddy to an LLM and runs the
// Anthropic Agent SDK / Computer Use multi-turn tool loop across all providers:
//   "none"      — no-op (local routes only)
//   "openai"    — OpenAI-compatible /v1/chat/completions with function tools
//   "ollama"    — local Ollama /api/chat with tools
//   "anthropic" — Anthropic Messages API (/v1/messages) with Agent SDK & Computer Use tools
//   "custom"    — arbitrary OpenAI-compatible endpoint with function tools
//
// Supported Anthropic Agent SDK Tools:
//   - "bash"               — execute PowerShell / shell commands asynchronously
//   - "computer"           — Anthropic Computer Use (screenshot, mouse move/click/drag/scroll, keyboard type/key)
//   - "str_replace_editor" — Anthropic Text Editor (view, create, str_replace, insert, undo_edit)
//   - "glob_grep"          — Anthropic Glob & Grep file/code search (pattern matching & regex content search)
//   - "memory"             — Anthropic Memory tool (/memories persistent cross-session workspace: view, create, str_replace, insert, delete, rename)
//   - "web_search"         — Anthropic Web Search (live web search queries with source citations)
//   - "web_fetch"          — Anthropic Web Fetch (fetch URL content, links, and metadata)
//   - "tool_search"        — Anthropic Tool Search (BM25 natural language & Regex tool discovery + dynamic schema loading)
//   - "advisor"            — Anthropic Advisor Tool (high-reasoning strategic consultation, architecture review & verification)
//   - "todo_write"         — Anthropic Task & Desktop Context Tool (structured TodoWrite checklist, clipboard & system inspection)
//
// Safety layer (see AgentPolicy.h and docs/agent-safety.md):
//   - every tool call passes AgentPolicy (permission mode, deny/ask/allow
//     rules, guardrails); "ask" pauses the run and raises pendingApproval for
//     the UI, resolved with resolveApproval()
//   - tool inputs/outputs are checked by guardrails (secret leak, prompt
//     injection in fetched content), repeated identical calls are cut off
//   - old tool results are compacted before each model call, Anthropic
//     prompts are cached, transient API errors retry with backoff
//   - each run writes a JSON trace (model + tool spans) to tracesPath()
class BuddyProvider : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY apiBaseChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)

    // Anthropic Agent SDK & Computer Use settings
    Q_PROPERTY(bool agentMode READ agentMode WRITE setAgentMode NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowBash READ allowBash WRITE setAllowBash NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowComputer READ allowComputer WRITE setAllowComputer NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowEditor READ allowEditor WRITE setAllowEditor NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowCodeSearch READ allowCodeSearch WRITE setAllowCodeSearch NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowMemory READ allowMemory WRITE setAllowMemory NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowWebSearch READ allowWebSearch WRITE setAllowWebSearch NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowToolSearch READ allowToolSearch WRITE setAllowToolSearch NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowAdvisor READ allowAdvisor WRITE setAllowAdvisor NOTIFY agentConfigChanged)
    Q_PROPERTY(bool allowTodoTask READ allowTodoTask WRITE setAllowTodoTask NOTIFY agentConfigChanged)

    // Live tool-execution telemetry & Anthropic Agent SDK response metadata for the UI
    // Permission mode: "default" | "acceptEdits" | "plan" | "bypass".
    Q_PROPERTY(QString permissionMode READ permissionMode WRITE setPermissionMode NOTIFY agentConfigChanged)
    // Folder where acceptEdits auto-approves file edits (empty = ~/Noch Workspace).
    Q_PROPERTY(QString workspaceRoot READ workspaceRoot WRITE setWorkspaceRoot NOTIFY agentConfigChanged)
    Q_PROPERTY(bool redactSecrets READ redactSecrets WRITE setRedactSecrets NOTIFY agentConfigChanged)
    Q_PROPERTY(bool keepTraces READ keepTraces WRITE setKeepTraces NOTIFY agentConfigChanged)
    // Non-empty while a tool call waits for the user: { id, stepIndex, tool,
    // badge, icon, title, detail, reason, rule, secondsLeft }.
    Q_PROPERTY(QVariantMap pendingApproval READ pendingApproval NOTIFY pendingApprovalChanged)

    Q_PROPERTY(QVariantList toolSteps READ toolSteps NOTIFY toolStepsChanged)
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activeToolChanged)
    Q_PROPERTY(QString activeToolBadge READ activeToolBadge NOTIFY activeToolChanged)
    Q_PROPERTY(QString activeToolSummary READ activeToolSummary NOTIFY activeToolChanged)
    Q_PROPERTY(int agentIteration READ agentIteration NOTIFY activeToolChanged)
    Q_PROPERTY(QVariantMap responseMeta READ responseMeta NOTIFY responseMetaChanged)
    Q_PROPERTY(QVariantList agentTodos READ agentTodos NOTIFY agentTodosChanged)
    Q_PROPERTY(QVariantList citations READ citations NOTIFY responseMetaChanged)

public:
    explicit BuddyProvider(QObject *parent = nullptr);

    QString provider() const { return m_provider; }
    void setProvider(const QString &p);
    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &k);
    QString apiBase() const { return m_apiBase; }
    void setApiBase(const QString &b);
    QString model() const { return m_model; }
    void setModel(const QString &m);
    double temperature() const { return m_temperature; }
    void setTemperature(double t);
    bool enabled() const { return m_enabled; }
    bool needsApiKey() const;

    bool agentMode() const { return m_agentMode; }
    void setAgentMode(bool on);
    bool allowBash() const { return m_allowBash; }
    void setAllowBash(bool on);
    bool allowComputer() const { return m_allowComputer; }
    void setAllowComputer(bool on);
    bool allowEditor() const { return m_allowEditor; }
    void setAllowEditor(bool on);
    bool allowCodeSearch() const { return m_allowCodeSearch; }
    void setAllowCodeSearch(bool on);
    bool allowMemory() const { return m_allowMemory; }
    void setAllowMemory(bool on);
    bool allowWebSearch() const { return m_allowWebSearch; }
    void setAllowWebSearch(bool on);
    bool allowToolSearch() const { return m_allowToolSearch; }
    void setAllowToolSearch(bool on);
    bool allowAdvisor() const { return m_allowAdvisor; }
    void setAllowAdvisor(bool on);
    bool allowTodoTask() const { return m_allowTodoTask; }
    void setAllowTodoTask(bool on);

    QString permissionMode() const { return m_permissionMode; }
    void setPermissionMode(const QString &m);
    QString workspaceRoot() const { return m_workspaceRoot; }
    void setWorkspaceRoot(const QString &r);
    bool redactSecrets() const { return m_redactSecrets; }
    void setRedactSecrets(bool on);
    bool keepTraces() const { return m_keepTraces; }
    void setKeepTraces(bool on);
    QVariantMap pendingApproval() const { return m_pendingApproval; }

    QVariantList toolSteps() const { return m_toolSteps; }
    QString activeTool() const { return m_activeTool; }
    QString activeToolBadge() const { return m_activeToolBadge; }
    QString activeToolSummary() const { return m_activeToolSummary; }
    int agentIteration() const { return m_agentIteration; }
    QVariantMap responseMeta() const { return m_responseMeta; }
    QVariantList agentTodos() const { return m_agentTodos; }
    QVariantList citations() const { return m_citations; }

    Q_INVOKABLE void sendMessage(const QString &text, const QVariantList &context = QVariantList());
    Q_INVOKABLE void runDirectCommand(const QString &command);
    Q_INVOKABLE void runDirectWebSearch(const QString &query);
    Q_INVOKABLE void runDirectToolSearch(const QString &query);
    Q_INVOKABLE void runDirectAdvisor(const QString &question);
    Q_INVOKABLE QString memoriesPath() const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void probe();
    Q_INVOKABLE void clearToolSteps();
    // Answer the pending approval: approve (once, or for the rest of the
    // session with always=true) or reject; a reject tells the model why.
    Q_INVOKABLE void resolveApproval(bool approve, bool always = false);
    Q_INVOKABLE QString tracesPath() const;
    Q_INVOKABLE QString effectiveWorkspace() const;

    Q_INVOKABLE QString chatUrl() const;
    Q_INVOKABLE QString modelsUrl() const;
    Q_INVOKABLE QString defaultBase() const;

signals:
    void providerChanged();
    void apiKeyChanged();
    void apiBaseChanged();
    void modelChanged();
    void temperatureChanged();
    void enabledChanged();
    void agentConfigChanged();

    void toolStepsChanged();
    void activeToolChanged();
    void responseMetaChanged();
    void agentTodosChanged();
    void pendingApprovalChanged();
    void toolExecuted(const QString &toolName, const QString &summary);

    void responseReady(const QString &text);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

    void testFinished(bool ok, const QString &message);
    void modelsFetched(const QStringList &models);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    struct PendingToolCall {
        QString id;
        QString name;
        QJsonObject input;
        int stepIndex = -1;
    };

    struct ToolOutcome {
        QString id;
        QString name;
        QString output;
        QString imageBase64;
        bool isError = false;
    };

    QString m_provider = "none";
    QString m_apiKey;
    QString m_apiBase;
    QString m_model = "gpt-4o-mini";
    double m_temperature = 0.7;
    bool m_enabled = false;

    bool m_agentMode = true;
    bool m_allowBash = true;
    bool m_allowComputer = true;
    bool m_allowEditor = true;
    bool m_allowCodeSearch = true;
    bool m_allowMemory = true;
    bool m_allowWebSearch = true;
    bool m_allowToolSearch = true;
    bool m_allowAdvisor = true;
    bool m_allowTodoTask = true;

    // ---- safety, recovery and tracing ------------------------------------
    AgentPolicy m_policy;
    QString m_permissionMode = QStringLiteral("default");
    QString m_workspaceRoot;
    bool m_redactSecrets = true;
    bool m_keepTraces = true;
    QVariantMap m_pendingApproval;
    QTimer m_approvalTimer;      // 1 s ticks for the countdown, auto-deny at 0
    int m_approvalSecondsLeft = 0;
    QTimer m_retryTimer;         // backoff before re-sending a failed model call
    int m_retryCount = 0;
    QHash<QString, int> m_callRepeats;  // name+input fingerprint -> count in this run
    QJsonArray m_traceSpans;
    QString m_traceId;
    QElapsedTimer m_modelTimer;
    int m_compactedResults = 0;
    int m_cachedTokens = 0;
    int m_deniedCalls = 0;

    QVariantList m_toolSteps;
    QString m_activeTool;
    QString m_activeToolBadge;
    QString m_activeToolSummary;
    int m_agentIteration = 0;
    bool m_toolsDisabledForTurn = false;
    QString m_accumulatedText;

    QVariantMap m_responseMeta;
    QVariantList m_agentTodos;
    QVariantList m_citations;
    int m_totalInputTokens = 0;
    int m_totalOutputTokens = 0;
    QString m_lastModelUsed;
    QString m_lastStopReason;
    bool m_advisorUsedInTurn = false;
    QString m_advisorFocusInTurn;
    QString m_advisorInsightInTurn;
    QElapsedTimer m_turnTimer;
    QElapsedTimer m_stepTimer;
    QHash<QString, QString> m_fileUndoBackups;
    QSet<QString> m_unlockedDeferredTools;

    QJsonArray m_agentMessages;
    QList<PendingToolCall> m_pendingTools;
    PendingToolCall m_approvalCall;      // the call waiting on pendingApproval
    QList<ToolOutcome> m_completedTools;

    QPointer<QNetworkReply> m_chatReply;
    QPointer<QNetworkReply> m_probeReply;
    QPointer<QNetworkReply> m_webSearchReply;
    QPointer<QNetworkReply> m_advisorReply;
    QPointer<QProcess> m_activeProcess;
    QNetworkAccessManager m_nam;

    bool hasAnyToolEnabled() const;
    QString systemPrompt() const;
    QJsonArray buildToolsArray() const;
    QString buildUrl() const;
    QJsonObject buildAgentPayload() const;
    QNetworkRequest buildRequest(const QString &url) const;

    void dispatchAgentTurn();
    void executeToolCall(const PendingToolCall &call);
    void gateAndExecute(const PendingToolCall &call);
    void setStepFields(int stepIndex, const QVariantMap &fields);
    void clearPendingApproval();
    void onApprovalTick();
    int compactContext();
    bool scheduleRetry(int status, QNetworkReply::NetworkError err, const QByteArray &retryAfter);
    QString guardToolOutput(const PendingToolCall &call, const QString &output, bool *flagged) const;
    QString redact(const QString &text) const;
    void addSpan(const QJsonObject &span);
    void finishTrace(const QString &outcome, const QString &detail = QString());
    void emitFinal(const QString &text);
    void processNextToolCall();
    void finishCurrentToolCall(const PendingToolCall &call, const QString &output,
                               const QString &imageBase64, bool isError);
    void updateResponseMeta();
    void addCitation(const QString &title, const QString &url);

    void executeBashTool(const PendingToolCall &call);
    void executeComputerTool(const PendingToolCall &call);
    void executeEditorTool(const PendingToolCall &call);
    void executeCodeSearchTool(const PendingToolCall &call);
    void executeMemoryTool(const PendingToolCall &call);
    void executeWebSearchTool(const PendingToolCall &call);
    void executeWebFetchTool(const PendingToolCall &call);
    void executeToolSearchTool(const PendingToolCall &call);
    void executeAdvisorTool(const PendingToolCall &call);
    void executeTodoTaskTool(const PendingToolCall &call);

    static QString summarizeToolCall(const QString &name, const QJsonObject &input);
    static QString detailForToolCall(const QString &name, const QJsonObject &input);
    static QString iconForTool(const QString &name, const QJsonObject &input);
    static QString badgeForTool(const QString &name, const QJsonObject &input);

    QString extractText(const QJsonDocument &doc) const;
    static QString errorText(const QByteArray &body, int status,
                             const QString &fallback);
    static QStringList modelsIn(const QJsonDocument &doc, const QString &provider);
    static QString trimBase(const QString &base);
    void finishProbe(bool transportOk, int status, const QByteArray &body,
                     const QString &fallback);
    void setActiveToolState(const QString &tool, const QString &summary);
};
