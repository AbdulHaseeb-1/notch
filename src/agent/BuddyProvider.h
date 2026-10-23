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
#include "ComputerUse.h"
#include "HookBus.h"
#include "PlanValidator.h"
#include "ProjectContext.h"
#include "RunJournal.h"
#include "SessionFormat.h"
#include "SavedRules.h"
#include "MusicBackend.h"
#include "StreamAccumulator.h"
#include "TurnClassifier.h"
#include "UserQuestions.h"

// Backend that connects Noch's desktop buddy to an LLM and runs the
// Anthropic Agent SDK / Computer Use multi-turn tool loop across all providers:
//   "none"      — no-op (local routes only)
//   "openai"    — OpenAI-compatible /v1/chat/completions with function tools
//   "opencode"  — OpenCode Zen (OpenAI-compatible /v1/chat/completions;
//                 the *-free models cost nothing with a free Zen key)
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
    Q_PROPERTY(bool allowTodoTask READ allowTodoTask WRITE setAllowTodoTask NOTIFY agentConfigChanged)
    // Clarifying questions: Buddy may pause to ask a multiple-choice question.
    Q_PROPERTY(bool allowAskUser READ allowAskUser WRITE setAllowAskUser NOTIFY agentConfigChanged)
    // Lets Buddy put a screenshot of the screen into the chat for the user to see
    // (share_screenshot). The image stays on this computer: the model is only told it was shown.
    Q_PROPERTY(bool allowScreenshots READ allowScreenshots WRITE setAllowScreenshots NOTIFY agentConfigChanged)
    // Lets Buddy control the music pill (the `music` tool): play, pause, skip, volume, Noch's queue.
    Q_PROPERTY(bool allowMusic READ allowMusic WRITE setAllowMusic NOTIFY agentConfigChanged)

    // Live tool-execution telemetry & Anthropic Agent SDK response metadata for the UI
    // Permission mode: "default" | "acceptEdits" | "plan" | "bypass".
    Q_PROPERTY(QString permissionMode READ permissionMode WRITE setPermissionMode NOTIFY agentConfigChanged)
    // Folder where acceptEdits auto-approves file edits (empty = ~/Noch Workspace).
    Q_PROPERTY(QString workspaceRoot READ workspaceRoot WRITE setWorkspaceRoot NOTIFY agentConfigChanged)
    Q_PROPERTY(bool redactSecrets READ redactSecrets WRITE setRedactSecrets NOTIFY agentConfigChanged)
    Q_PROPERTY(bool keepTraces READ keepTraces WRITE setKeepTraces NOTIFY agentConfigChanged)
    // Streaming (SSE) replies: streamText is the answer text of the model
    // turn in flight, updated live (coalesced to ~25 fps, secrets redacted).
    Q_PROPERTY(bool streamReplies READ streamReplies WRITE setStreamReplies NOTIFY agentConfigChanged)
    // Show what Buddy is doing while it works (tool steps, the draft as it is written,
    // the model's reasoning). Off: a quiet bubble that only shows the answer; approvals
    // and questions still appear, because the run waits on them.
    Q_PROPERTY(bool showThinking READ showThinking WRITE setShowThinking NOTIFY agentConfigChanged)
    // Computer use with Anthropic's own tool definitions (the computer toolset
    // or, for older models, computer_20251124 / computer_20250124) instead of
    // Noch's function-style look-alike. Only the Anthropic provider can use
    // them; anything else always gets the look-alike.
    Q_PROPERTY(bool officialComputerTools READ officialComputerTools WRITE setOfficialComputerTools NOTIFY agentConfigChanged)
    // Screenshot detail for computer use: "sharp" (up to 1920x1080, lossless)
    // or "standard" (1280x800, JPEG).
    Q_PROPERTY(QString computerDetail READ computerDetail WRITE setComputerDetail NOTIFY agentConfigChanged)
    // Tool rounds per task (each round may hold several actions).
    Q_PROPERTY(int maxSteps READ maxSteps WRITE setMaxSteps NOTIFY agentConfigChanged)
    // Which computer tool the next request uses: "toolset", "computer_20251124",
    // "computer_20250124" or "custom".
    Q_PROPERTY(QString computerToolMode READ computerToolMode NOTIFY agentConfigChanged)
    // Agent cursor / edge glow / control HUD state for computer use.
    Q_PROPERTY(ComputerUse *computer READ computer CONSTANT)
    Q_PROPERTY(QString streamText READ streamText NOTIFY streamTextChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamTextChanged)
    // JEV request classifier (see TurnClassifier.h): a small typed model that labels
    // each request before the agent loop starts. It has its own endpoint, key and
    // model, independent of the chat provider above. Off by default.
    Q_PROPERTY(bool classifierEnabled READ classifierEnabled WRITE setClassifierEnabled NOTIFY classifierConfigChanged)
    Q_PROPERTY(QString classifierBase READ classifierBase WRITE setClassifierBase NOTIFY classifierConfigChanged)
    Q_PROPERTY(QString classifierKey READ classifierKey WRITE setClassifierKey NOTIFY classifierConfigChanged)
    Q_PROPERTY(QString classifierModel READ classifierModel WRITE setClassifierModel NOTIFY classifierConfigChanged)
    // The last valid result for the current run: { valid, intent, risk, effort,
    // agent, tools, confidence, summary }; empty when the run was not classified.
    Q_PROPERTY(QVariantMap lastClassification READ lastClassification NOTIFY lastClassificationChanged)
    // Non-empty while a tool call waits for the user: { id, stepIndex, tool,
    // badge, icon, title, detail, reason, rule, secondsLeft }.
    Q_PROPERTY(QVariantMap pendingApproval READ pendingApproval NOTIFY pendingApprovalChanged)
    // Non-empty while Buddy waits for answers: { id, stepIndex, title, secondsLeft,
    // questions: [{ question, header, multiSelect, options: [{ label, description }] }] }.
    Q_PROPERTY(QVariantMap pendingQuestion READ pendingQuestion NOTIFY pendingQuestionChanged)
    // The file changes the run that just finished made and that can be undone:
    // { runId, count, created, modified, deleted, files: [{ path, action }],
    //   untracked: [{ path, reason }] }. Empty when it changed nothing.
    Q_PROPERTY(QVariantMap lastRunChanges READ lastRunChanges NOTIFY lastRunChangesChanged)
    // The "always allow" rules the user saved (see AgentPolicy::SavedRule), for
    // the settings list: [{ id, tool, kind, pattern, scope, label, createdAt }].
    Q_PROPERTY(QVariantList savedRules READ savedRules NOTIFY savedRulesChanged)

    Q_PROPERTY(QVariantList toolSteps READ toolSteps NOTIFY toolStepsChanged)
    // Images Buddy put into this reply: [{ url, path, caption, width, height }].
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activeToolChanged)
    Q_PROPERTY(QString activeToolIcon READ activeToolIcon NOTIFY activeToolChanged)
    Q_PROPERTY(QString activeToolBadge READ activeToolBadge NOTIFY activeToolChanged)
    Q_PROPERTY(QString activeToolSummary READ activeToolSummary NOTIFY activeToolChanged)
    Q_PROPERTY(int agentIteration READ agentIteration NOTIFY activeToolChanged)
    Q_PROPERTY(QVariantMap responseMeta READ responseMeta NOTIFY responseMetaChanged)
    Q_PROPERTY(QVariantList agentTodos READ agentTodos NOTIFY agentTodosChanged)
    Q_PROPERTY(QVariantList citations READ citations NOTIFY responseMetaChanged)

public:
    ComputerUse *computer() const { return m_computer; }
    // Tests pin the monitor layout instead of reading it from Qt.
    void setComputerAutoScreens(bool on) { m_computerAutoScreens = on; m_computer->setAutoScreens(on); }
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
    bool allowTodoTask() const { return m_allowTodoTask; }
    void setAllowTodoTask(bool on);
    bool allowAskUser() const { return m_allowAskUser; }
    void setAllowAskUser(bool on);
    bool allowScreenshots() const { return m_allowScreenshots; }
    void setAllowScreenshots(bool on);
    bool allowMusic() const { return m_allowMusic; }
    void setAllowMusic(bool on);
    // The music pill the `music` tool drives; not owned. Without one the tool is not offered.
    void setMusicBackend(MusicBackend *backend);
    bool musicAvailable() const { return m_allowMusic && m_musicBackend; }

    QString permissionMode() const { return m_permissionMode; }
    void setPermissionMode(const QString &m);
    QString workspaceRoot() const { return m_workspaceRoot; }
    void setWorkspaceRoot(const QString &r);
    bool redactSecrets() const { return m_redactSecrets; }
    void setRedactSecrets(bool on);
    bool keepTraces() const { return m_keepTraces; }
    void setKeepTraces(bool on);
    bool streamReplies() const { return m_streamReplies; }
    void setStreamReplies(bool on);
    bool showThinking() const { return m_showThinking; }
    void setShowThinking(bool on);
    QString streamText() const { return m_streamText; }
    bool streaming() const { return m_streamingNow; }
    bool officialComputerTools() const { return m_officialComputer; }
    void setOfficialComputerTools(bool on);
    QString computerDetail() const { return m_computerDetail; }
    void setComputerDetail(const QString &d);
    int maxSteps() const { return m_maxSteps; }
    void setMaxSteps(int n);
    // The tool-version ladder. A model starts at the newest version it
    // accepts; a 400 that names the computer tool steps down one rung.
    enum class ComputerMode { Toolset = 0, V20251124 = 1, V20250124 = 2, Custom = 3 };
    static ComputerMode computerModeForModel(const QString &model);
    static QString computerModeName(ComputerMode m);
    ComputerMode computerMode() const;
    QString computerToolMode() const { return computerModeName(computerMode()); }
    QVariantMap pendingApproval() const { return m_pendingApproval; }
    QVariantMap pendingQuestion() const { return m_pendingQuestion; }
    QVariantMap lastRunChanges() const { return m_lastRunChanges; }
    QVariantList savedRules() const { return m_savedRules.toVariant(); }
    Q_INVOKABLE void removeSavedRule(const QString &id);
    Q_INVOKABLE void clearSavedRules();
    Q_INVOKABLE QString savedRulesPath() const;
    void setSavedRulesPath(const QString &path);

    QVariantList toolSteps() const { return m_toolSteps; }
    QVariantList attachments() const { return m_attachments; }
    QString activeTool() const { return m_activeTool; }
    QString activeToolIcon() const { return m_activeToolIcon; }
    QString activeToolBadge() const { return m_activeToolBadge; }
    QString activeToolSummary() const { return m_activeToolSummary; }
    int agentIteration() const { return m_agentIteration; }
    QVariantMap responseMeta() const { return m_responseMeta; }
    QVariantList agentTodos() const { return m_agentTodos; }
    QVariantList citations() const { return m_citations; }

    // `spoken`: the final reply will be read aloud (the request came by voice, or
    // every answer is read out), so the model is told to keep it to a sentence or two.
    Q_INVOKABLE void sendMessage(const QString &text, const QVariantList &context = QVariantList(),
                                 bool spoken = false);
    Q_INVOKABLE void runDirectCommand(const QString &command);
    Q_INVOKABLE void runDirectWebSearch(const QString &query);
    Q_INVOKABLE QString memoriesPath() const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void probe();
    Q_INVOKABLE void clearToolSteps();
    // Answer the pending approval: approve (once, or for the rest of the
    // session with always=true) or reject; a reject tells the model why.
    Q_INVOKABLE void resolveApproval(bool approve, bool always = false);
    // Answer the pending question: one { selected: [option index, ...], other: "text" }
    // per question. Ignored until every question has an answer.
    Q_INVOKABLE void answerQuestion(const QVariantList &answers);
    // "Skip": the model is told to carry on with its best assumption.
    Q_INVOKABLE void skipQuestion();
    // Answers whatever is waiting (an approval or a question) from words the user said,
    // for a voice conversation: "yes, go ahead", "the second one". Only ever picks among
    // what was offered; returns { kind: "approval" | "question" | "none", ok, message }.
    // ok is false when the words were not clear enough (the thing stays pending).
    Q_INVOKABLE QVariantMap answerPendingSpoken(const QString &said);
    // What to read out to the user for the pending approval or question ("" when none).
    Q_INVOKABLE QString pendingSpokenPrompt() const;
    // How long a question waits before it is skipped (tests shorten it).
    void setQuestionTimeoutSeconds(int seconds) { m_questionTimeoutSec = qMax(1, seconds); }
    // Undo a run's file changes. undoLastRun() takes the newest run that changed
    // anything, which may be older than the last one. Files changed since the
    // run are left alone and listed under `conflicts` unless force is set.
    // Returns { ok, runId, restored, removed, conflicts, failed, message }.
    Q_INVOKABLE QVariantMap pendingUndo() const;
    Q_INVOKABLE QVariantMap undoLastRun(bool force = false);
    Q_INVOKABLE QVariantMap undoRun(const QString &runId, bool force = false);
    Q_INVOKABLE QString undoPath() const;
    void setUndoPath(const QString &path) { m_journal.setRoot(path); }
    // Slash commands. resolveSlash() reads what the user typed:
    //   { kind: "none" }                                  not a command
    //   { kind: "builtin", name, args }                   the UI does it (/new, /undo ...)
    //   { kind: "prompt", name, args, prompt, source }    a user's template, expanded: send `prompt`
    //   { kind: "unknown", name, message, suggestion }
    Q_INVOKABLE QVariantMap resolveSlash(const QString &text) const;
    // [{ name, description, argumentHint, source }] for the suggestion list; `prefix` is typed after the slash.
    Q_INVOKABLE QVariantList slashCommands(const QString &prefix = QString()) const;
    // The instruction files Buddy reads for this workspace, read fresh:
    // { empty, text, truncated, sources: [{ name, path, bytes, truncated }] }.
    Q_INVOKABLE QVariantMap projectInstructions() const;
    // /init: write a starter NOCH.md into the workspace unless it has instructions already.
    Q_INVOKABLE QVariantMap initProjectInstructions();
    // Noch's own data folder (user-level NOCH.md and commands/). Tests point it elsewhere.
    Q_INVOKABLE QString userDataDir() const;
    void setUserDataDir(const QString &dir) { m_userDataDir = dir; }
    // Write a rolling summary of chat turns (see ChatSessions): one plain
    // model call with no tools, queued behind any other. `prior` is the summary so far,
    // `turns` the [{ q, a }] to fold in, `upTo` how many turns the result will cover.
    // Answers with sessionSummaryReady() or sessionSummaryFailed(); never touches
    // the run in progress.
    Q_INVOKABLE void summarizeSession(const QString &sessionId, const QString &prior,
                                      const QVariantList &turns, int upTo);
    Q_INVOKABLE QString tracesPath() const;
    Q_INVOKABLE QString effectiveWorkspace() const;
    void syncPrivatePaths();

    Q_INVOKABLE QString chatUrl() const;
    Q_INVOKABLE QString modelsUrl() const;
    Q_INVOKABLE QString defaultBase() const;

    bool classifierEnabled() const { return m_classifier->enabled(); }
    void setClassifierEnabled(bool on);
    QString classifierBase() const { return m_classifier->base(); }
    void setClassifierBase(const QString &base);
    QString classifierKey() const { return m_classifier->key(); }
    void setClassifierKey(const QString &key);
    QString classifierModel() const { return m_classifier->model(); }
    void setClassifierModel(const QString &model);
    QVariantMap lastClassification() const;
    // The URL the classifier posts to (for the settings hint).
    Q_INVOKABLE QString classifierUrl() const { return m_classifier->endpoint(); }
    // Seconds the classifier is resting after repeated failures (0 = active).
    Q_INVOKABLE int classifierRestSeconds() const { return m_classifier->restSecondsLeft(); }
    // Send a sample request to the classifier endpoint; answers classifierTestFinished().
    // Works whether or not the classifier is switched on.
    Q_INVOKABLE void testClassifier() { m_classifier->test(); }
    // Tests tune the classifier's timeouts and resting period.
    TurnClassifier *classifierClient() const { return m_classifier; }

signals:
    void providerChanged();
    void apiKeyChanged();
    void apiBaseChanged();
    void modelChanged();
    void temperatureChanged();
    void enabledChanged();
    void agentConfigChanged();

    void toolStepsChanged();
    void attachmentsChanged();
    void activeToolChanged();
    void responseMetaChanged();
    void agentTodosChanged();
    void pendingApprovalChanged();
    void pendingQuestionChanged();
    void lastRunChangesChanged();
    void sessionSummaryReady(const QString &sessionId, const QString &summary, int upTo);
    void sessionSummaryFailed(const QString &sessionId, const QString &error);
    void savedRulesChanged();
    void streamTextChanged();
    void toolExecuted(const QString &toolName, const QString &summary);

    void responseReady(const QString &text);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

    void testFinished(bool ok, const QString &message);
    void modelsFetched(const QStringList &models);
    void classifierConfigChanged();
    void lastClassificationChanged();
    void classifierTestFinished(bool ok, const QString &message);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    friend class TstBuddyAgent;  // audit-driven tests for the structured ladder
    friend class TstToolRepair;  // (HookBus / repair tests already use free functions)
    struct PendingToolCall {
        QString id;
        QString name;
        QJsonObject input;
        int stepIndex = -1;
        QString toolsetName;   // set when the model called a toolset member (e.g. left_click of "computer")
        QString rawArguments;  // the model's raw argument text when it was not a JSON object (for repair)
        QString repairError;   // non-empty when the call could not be repaired: answered, never executed
    };

    struct ToolOutcome {
        QString id;
        QString name;
        QString output;
        QString imageBase64;
        QString imageMediaType;
        QString toolsetName;   // echoed on the tool_result of a toolset member
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
    bool m_allowTodoTask = true;
    bool m_allowAskUser = true;
    bool m_allowScreenshots = true;
    bool m_allowMusic = true;
    MusicBackend *m_musicBackend = nullptr;

    // ---- safety, recovery and tracing ------------------------------------
    AgentPolicy m_policy;
    // P3 hook bus: lifecycle events (PreToolUse/PostToolUse/Stop/
    // SessionStart/PreCompact) with allow/deny/ask/defer + rewrites.
    HookBus m_hookBus;
    void registerBuiltinHooks();
    // Last PreToolUse verdict, stashed by the policy hook for the gate's
    // bookkeeping (spans, session grants, approval UI).
    AgentPolicy::Decision m_lastPolicyDecision;
    // Last PostToolUse injection flag, stashed by the output-guard hook.
    bool m_outputGuardFlagged = false;
    QString m_permissionMode = QStringLiteral("default");
    QString m_workspaceRoot;
    bool m_redactSecrets = true;
    bool m_keepTraces = true;
    QVariantMap m_pendingApproval;
    QTimer m_approvalTimer;      // 1 s ticks for the countdown, auto-deny at 0
    int m_approvalSecondsLeft = 0;
    // The question being asked (ask_user_question) and its countdown.
    QString m_instructionsSection;   // system-prompt text from the instruction files, read once per run
    QString m_userDataDir;
    SavedRules m_savedRules;
    AgentPolicy::SavedRule m_approvalRule;   // what "Always allow" on the pending card would save
    RunJournal m_journal;
    QVariantMap m_lastRunChanges;
    QVariantMap m_pendingQuestion;
    QList<uq::Question> m_questions;
    PendingToolCall m_questionCall;
    QTimer m_questionTimer;
    int m_questionSecondsLeft = 0;
    int m_questionTimeoutSec = 300;
    QTimer m_retryTimer;         // backoff before re-sending a failed model call
    int m_retryCount = 0;
    QHash<QString, int> m_callRepeats;  // name+input fingerprint -> count in this run
    QString m_lastComputerFp;           // computer calls: the previous call's fingerprint ...
    int m_computerRepeat = 0;           // ... and how many times in a row it has come
    QJsonArray m_traceSpans;
    QString m_traceId;
    QElapsedTimer m_modelTimer;
    int m_compactedResults = 0;
    mutable int m_lastCacheMark = -1;   // message index the previous request ended its cacheable prefix at
    int m_cachedTokens = 0;
    int m_deniedCalls = 0;
    int m_repairedCalls = 0;   // tool calls normalized from near-miss arguments this run
    int m_malformedCalls = 0;  // tool calls answered with a correction nudge this run
    int m_totalToolCalls = 0;  // all tool calls seen this run (for repair-rate alerting)
    // P1 bounded repair: unrepairable-call count PER TOOL (L1: a single
    // "last failed tool" slot let alternating broken calls dodge the
    // escalation). Two failures of the same tool → escalate, not loop.
    QHash<QString, int> m_repairFailCounts;
    bool m_repairRateAlerted = false;

    QVariantList m_toolSteps;
    QVariantList m_attachments;
    QString m_activeTool;
    QString m_activeToolIcon;
    QString m_activeToolBadge;
    QString m_activeToolSummary;
    int m_agentIteration = 0;
    bool m_toolsDisabledForTurn = false;
    // A tool call arrived without an id this turn: a 400 mentioning tools is
    // our own malformed tool_result, not a capability signal.
    bool m_emptyToolIdInTurn = false;
    QString m_accumulatedText;
    // Output-limit continuation (stop_reason max_tokens / finish_reason
    // length): the text of the cut turns so far, how many continuations this
    // run asked for, and whether the answer was still cut after the last one.
    QString m_continuedText;
    int m_continuations = 0;
    bool m_truncated = false;
    int m_outputTokenLimit = 8192;   // Anthropic max_tokens; lowered if the model rejects it
    bool m_officialComputer = true;
    QString m_computerDetail = QStringLiteral("sharp");
    int m_maxSteps = 40;
    int m_computerFallback = 0;      // rungs the 400 fallback has stepped down; reset with the model
    bool m_computerBatchFailed = false;   // an action in this turn's batch failed: the rest are not run

    QVariantMap m_responseMeta;
    QVariantList m_agentTodos;
    QVariantList m_citations;
    int m_totalInputTokens = 0;
    int m_totalOutputTokens = 0;
    QString m_lastModelUsed;
    QString m_lastStopReason;
    QString m_reasoningInTurn;   // the model reasoning summary, shown when show-thinking is on
    QElapsedTimer m_turnTimer;
    QElapsedTimer m_stepTimer;
    QHash<QString, QString> m_fileUndoBackups;
    // ---- P5 plan-then-act ------------------------------------------------
    // A validated plan the model produced for this run. Steps execute in
    // order; 2 consecutive failures trigger a bounded replan (max 2).
    QList<PlanStep> m_plan;
    bool m_planActive = false;
    int m_planIndex = 0;
    int m_consecutivePlanFailures = 0;
    int m_replanCount = 0;
    // Note to send the model after a plan is accepted/rejected (when the
    // turn had no tool calls to carry it).
    QString m_planNote;
    // P5: set when 2 consecutive plan steps fail and a replan is needed.
    bool m_replanNeeded = false;
    // P8: distilled procedure text from a plan that completed this run;
    // saved to /memories/procedures/ by the end-of-run extractor.
    QString m_pendingProcedure;

    QJsonArray m_agentMessages;
    QList<PendingToolCall> m_pendingTools;
    PendingToolCall m_approvalCall;
    // streaming
    bool wantsStreaming() const;
    void onStreamReadyRead();
    void setStreamText(const QString &text, bool live);
    bool m_streamReplies = true;
    bool m_showThinking = true;
    ComputerUse *m_computer = nullptr;
    bool m_computerAutoScreens = true;
    StreamAccumulator m_stream;
    bool m_streamConsumed = false;    // this reply's body is being parsed as SSE
    bool m_streamingNow = false;
    QString m_streamText;
    QTimer m_streamEmitTimer;
    QString m_memoryIndexSnapshot;   // memoryIndex() taken at run start
    bool m_spokenRun = false;        // this run's reply is read aloud: short, plain prose
    // JEV classifier: runs between "the user sent a request" and the first model
    // turn. Fails open; a valid, confident result only narrows the run.
    TurnClassifier *m_classifier = nullptr;
    turnclass::Classification m_classification;   // this run's valid result (invalid = none)
    bool m_classifyPending = false;               // the first model turn waits for the classifier
    void dispatchAfterClassify(const QString &text);
    void onClassified(quint64 token, const turnclass::Classification &result, int ms, const QString &error);
    QString memoryIndex(const QString &query = QString()) const;
    quint64 m_runGeneration = 0;     // bumped by cancel(); stale deferred steps check it
    QString m_stepStatusOverride;   // set just before finishCurrentToolCall for "denied"
    QList<ToolOutcome> m_completedTools;

    QPointer<QNetworkReply> m_chatReply;
    QPointer<QNetworkReply> m_probeReply;
    QPointer<QNetworkReply> m_webSearchReply;
    // Summaries of chat turns, one request at a time.
    struct SummaryJob {
        QString id;
        QString prior;
        QVariantList turns;
        int upTo = 0;
    };
    QList<SummaryJob> m_summaryQueue;
    SummaryJob m_summaryActive;
    QPointer<QNetworkReply> m_summaryReply;
    void startNextSummary();
    void onSummaryReply(bool transportOk, int status, const QByteArray &body, const QString &errString);
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
    // P5: detect a ```plan block in model text, validate, and activate it.
    // Returns a note for the model (empty when no plan block was present).
    QString checkForPlan(const QString &text);
    void setStepFields(int stepIndex, const QVariantMap &fields);
    void clearPendingApproval();
    void clearPendingQuestion();
    void beginJournalRun();
    void finalizeJournal();
    void journalBefore(const PendingToolCall &call);
    void onApprovalTick();
    void onQuestionTick();
    void executeAskUserTool(const PendingToolCall &call);
    void executeScreenshotTool(const PendingToolCall &call);
    void executeMusicTool(const PendingToolCall &call);
    QString saveScreenshot(const QImage &image, QString *error) const;
    void addAttachment(const QVariantMap &attachment);
    int compactContext();
    bool scheduleRetry(int status, QNetworkReply::NetworkError err, const QByteArray &retryAfter);
    QString guardToolOutput(const PendingToolCall &call, const QString &output, bool *flagged) const;
    QString redact(const QString &text) const;
    void addSpan(const QJsonObject &span);
    void finishTrace(const QString &outcome, const QString &detail = QString());
    void emitFinal(const QString &text);
    void processNextToolCall();
    void finishCurrentToolCall(const PendingToolCall &call, const QString &output,
                               const QString &imageBase64, bool isError,
                               const QString &imageMediaType = QString());
    void applyComputerProfile() const;
    void addBetaHeaders(QNetworkRequest &request, const QJsonObject &payload) const;
    bool historyHasComputerToolUse() const;
    static bool mentionsComputerTool(const QString &error);
    // P2 structured-output ladder: plain text+parser < JSON mode <
    // function calling < constrained decoding. Ollama's `format` with a
    // JSON Schema is constrained decoding; plain-text models get the
    // parser. Validation (P1) still applies on top.
    enum class StructuredLevel { TextParser, JsonMode, FunctionCalling };
    StructuredLevel structuredLevel() const;
    // The JSON Schema used as Ollama's `format` in JsonMode: a tool-call
    // envelope the model must fill. One schema for every tool keeps the
    // diet small and the parser uniform.
    QJsonObject textProtocolSchema() const;
    void updateResponseMeta();
    // Model-agnostic harness: whether the configured model can process
    // images. Decided by provider and model name; unknown models count as
    // blind, because an image payload to a text-only model fails the turn.
    bool modelVisionCapable() const;
    void addCitation(const QString &title, const QString &url);

    void executeBashTool(const PendingToolCall &call);
    void executeComputerTool(const PendingToolCall &call);
    void executeEditorTool(const PendingToolCall &call);
    void executeCodeSearchTool(const PendingToolCall &call);
    void executeMemoryTool(const PendingToolCall &call);
    // P8: BM25 retrieval over the memory store.
    void executeMemorySearchTool(const PendingToolCall &call);
    // P8: end-of-run memory pipeline — extract atomic facts from the run's
    // user messages, BM25-dedupe against the store, ADD/UPDATE, and distill
    // completed plans into procedures.
    void extractRunMemories();
    void executeWebSearchTool(const PendingToolCall &call);
    void executeWebFetchTool(const PendingToolCall &call);
    void executeTodoTaskTool(const PendingToolCall &call);

    static QString summarizeToolCall(const QString &name, const QJsonObject &input);
    static QString detailForToolCall(const QString &name, const QJsonObject &input);
    static QString iconForTool(const QString &name, const QJsonObject &input);
    static QString badgeForTool(const QString &name, const QJsonObject &input);

    QString extractText(const QJsonDocument &doc) const;
    QString rawAnswerText(const QJsonDocument &doc) const;
    static QString errorText(const QByteArray &body, int status,
                             const QString &fallback);
    static QStringList modelsIn(const QJsonDocument &doc, const QString &provider);
    static QString trimBase(const QString &base);
    void finishProbe(bool transportOk, int status, const QByteArray &body,
                     const QString &fallback);
    void setActiveToolState(const QString &tool, const QString &summary,
                            const QJsonObject &input = QJsonObject());
};
