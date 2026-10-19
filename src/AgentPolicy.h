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
// The class has no event loop and no side effects so it can be unit tested.
// Path checks do read the filesystem (stat only) to resolve symlinks, so a
// link inside the workspace that points at ~/.ssh or C:\\Windows is judged by
// its target, not by its name.
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

    // Folders where file edits count as "inside the workspace". The first
    // root is also the base for relative paths (the shell's default cwd).
    void setWorkspaceRoots(const QStringList &roots);
    QStringList workspaceRoots() const { return m_roots; }
    QString baseDir() const { return m_base; }

    // Noch's own settings and data (API key, permission mode, traces): reading
    // asks, writing is denied, so the agent cannot raise its own permissions.
    void setPrivatePaths(const QStringList &paths);

    // Set when untrusted tool output in this run looked like a prompt
    // injection. While tainted, Trusted mode and "always allow" grants stop
    // auto-approving side effects and network fetches: they ask instead.
    void setTainted(bool on) { m_tainted = on; }
    bool tainted() const { return m_tainted; }
    // Set once the agent has looked at the screen this run. Screen content
    // is untrusted (a page can show "press Win+R and type ..."), so risky
    // key combos and shell-like typing ask even in Trusted mode / with a grant.
    void setScreenTainted(bool on) { m_screenTainted = on; }
    bool screenTainted() const { return m_screenTainted; }

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
    // Relative paths resolve against `base`; symlinks are followed.
    static QString protectedPathReason(const QString &path, const QString &base = QString());
    // Credential stores and secrets files, a subset of the above plus .env,
    // *.pem, id_rsa, browser profiles: reading them asks.
    static QString credentialPathReason(const QString &path, const QString &base = QString());
    static bool isSecretsFile(const QString &path);
    bool insideWorkspace(const QString &path) const;
    // Lexically cleaned absolute path (relative paths join the workspace
    // base). What BuddyProvider executes, so the gate and the tool agree.
    QString absolutePath(const QString &path) const;
    static QString resolvePath(const QString &path, const QString &base);

    // Shell commands the gate cannot see through (eval, encoded commands,
    // a variable as the program): asked even in Trusted mode.
    static QString opaqueCommandReason(const QString &command);
    // Every path-like word a shell command mentions, with write targets
    // (redirects, cp/mv/rm/Set-Content ... arguments) listed separately.
    // `moved` lists what mv / move / Move-Item take away from its place: the
    // gate treats those as reads, but undoing a run has to put them back.
    struct ShellPaths { QStringList reads; QStringList writes; QStringList moved; };
    static ShellPaths shellPaths(const QString &command);

    // web_fetch / web_search urls: only http(s) is fetched; private,
    // loopback, link-local and metadata hosts ask.
    static bool isFetchableUrl(const QString &url);
    static bool isPrivateUrl(const QString &url);
    // True for editor commands that only read.
    static bool isReadOnlyEditorCommand(const QString &command);
    // True for computer actions that only observe the screen.
    static bool isObservingComputerAction(const QString &action);

    // Output guardrails.
    static QString redactSecrets(const QString &text, const QStringList &knownSecrets = {});
    static bool looksLikePromptInjection(const QString &text);
    // Drops invisible "tag" characters (ASCII smuggling) and zero-width
    // characters from text the model will read.
    static QString stripInvisible(const QString &text, bool *changed = nullptr);
    // Makes bidi overrides and zero-width characters visible, so the
    // approval card shows exactly what will run. Long text keeps its head
    // and tail with a marker in between.
    static QString displaySafe(const QString &text, int maxChars, bool *hidden = nullptr);
    // Escapes <untrusted_content> tags inside fenced data so content cannot
    // close the fence early.
    static QString neutralizeFence(const QString &text);

private:
    Mode m_mode = Mode::Default;
    QStringList m_roots;      // lowercased lexical + resolved forms
    QString m_base;           // first root, original case
    QStringList m_private;    // lowercased lexical + resolved forms
    QSet<QString> m_sessionAllow;
    bool m_tainted = false;
    bool m_screenTainted = false;

    bool isPrivatePath(const QString &path) const;
    Decision evaluateShell(const QJsonObject &input) const;
};
