#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

// Permission gate for Buddy's agent tools, evaluated before every tool call.
//
// Modelled on the Claude Agent SDK permission flow
// (docs.claude.com/en/docs/agent-sdk/permissions): guardrail hooks first, then
// deny rules, ask rules, the permission mode, allow rules, and finally the
// interactive approval callback (canUseTool) for anything still undecided.
// A deny always wins, even in the most permissive mode, and a small set of
// actions (critical-path removals, disk formatting, ...) can never be
// auto-approved. Human-in-the-loop approval follows the OpenAI Agents SDK
// "needs approval -> interruption -> approve/reject" pattern.
//
// The class is pure logic (no I/O, no Qt event loop) so it can be unit tested.
class AgentPolicy
{
public:
    enum class Mode {
        Default,     // read-only work runs; shell writes, edits and computer control ask
        AcceptEdits, // file edits inside the workspace auto-approved; shell/computer ask
        Plan,        // read-only: anything that changes the machine is denied
        Bypass,      // trusted: everything runs except hard-denied actions
    };

    enum class Verdict { Allow, Ask, Deny };

    struct Decision {
        Verdict verdict = Verdict::Ask;
        QString reason;   // shown to the user and, on deny, returned to the model
        QString rule;     // short machine tag: "read-only", "deny:rm-root", ...
    };

    static Mode modeFromString(const QString &s);
    static QString modeToString(Mode m);

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }

    // Folders where file edits count as "inside the workspace".
    void setWorkspaceRoots(const QStringList &roots);
    QStringList workspaceRoots() const { return m_roots; }

    // Session-scoped "always allow" grants from the approval card.
    void allowForSession(const QString &tool) { m_sessionAllow.insert(tool); }
    void clearSessionGrants() { m_sessionAllow.clear(); }
    bool sessionAllowed(const QString &tool) const { return m_sessionAllow.contains(tool); }

    Decision evaluate(const QString &tool, const QJsonObject &input) const;

    // ---- building blocks, public for tests -------------------------------
    // Hard guardrail for shell commands; returns a reason when it must never run.
    static QString dangerousCommandReason(const QString &command);
    // True for commands that only read state (ls, dir, git status, Get-ChildItem, ...).
    static bool isReadOnlyCommand(const QString &command);
    // Paths an editor/memory write must never touch (OS dirs, ssh keys, ...).
    static QString protectedPathReason(const QString &path);
    bool insideWorkspace(const QString &path) const;
    // True for editor commands that only read.
    static bool isReadOnlyEditorCommand(const QString &command);
    // True for computer actions that only observe the screen.
    static bool isObservingComputerAction(const QString &action);

    // Output guardrails.
    static QString redactSecrets(const QString &text, const QStringList &knownSecrets = {});
    static bool looksLikePromptInjection(const QString &text);

private:
    Mode m_mode = Mode::Default;
    QStringList m_roots;
    QSet<QString> m_sessionAllow;
};
