#include "AgentPolicyInternal.h"
#include "ComputerUseKeys.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

using namespace noch::agentpolicy;

AgentPolicy::Decision AgentPolicy::evaluateShell(const QJsonObject &input) const
{
    auto allow = [](const QString &rule, const QString &reason = QString()) {
        return Decision{ Verdict::Allow, reason, rule };
    };
    auto deny = [](const QString &rule, const QString &reason) {
        return Decision{ Verdict::Deny, reason, rule };
    };
    auto ask = [](const QString &rule, const QString &reason) {
        return Decision{ Verdict::Ask, reason, rule };
    };
    const QString cmd = input.value(QStringLiteral("command")).toString();
    const QString workDir = input.value(QStringLiteral("working_dir")).toString().trimmed();
    const QString base = workDir.isEmpty() ? m_base : lexical(workDir, m_base);

    // 1. Hard deny: never run, in any mode.
    const QString danger = dangerousCommandReason(cmd);
    if (!danger.isEmpty())
        return deny(QStringLiteral("deny:shell"), QStringLiteral("Blocked: %1.").arg(danger));

    // Working directory itself must not be a protected / private location.
    if (!workDir.isEmpty()) {
        const QString prot = protectedPathReason(workDir, m_base);
        if (!prot.isEmpty())
            return deny(QStringLiteral("deny:protected-cwd"),
                        QStringLiteral("Blocked: working directory %1.").arg(prot));
        if (isPrivatePath(workDir))
            return deny(QStringLiteral("deny:private-cwd"),
                        QStringLiteral("Blocked: working directory is Noch's own data folder."));
    }

    // Paths the command would write: protected / private / credentials => deny.
    const ShellPaths paths = shellPaths(cmd);
    bool writesInstructions = false;
    for (const QString &w : paths.writes) {
        writesInstructions = writesInstructions || isInstructionPath(lexical(w, base));
        const QString prot = protectedPathReason(w, base);
        if (!prot.isEmpty())
            return deny(QStringLiteral("deny:protected-path"), QStringLiteral("Blocked: writing %1.").arg(prot));
        if (isPrivatePath(w))
            return deny(QStringLiteral("deny:private-path"),
                        QStringLiteral("Blocked: writing to Noch's own settings or data."));
        const QString cred = credentialPathReason(w, base);
        if (!cred.isEmpty() && (cred.contains(QLatin1String("credentials"))
                                || cred.contains(QLatin1String("password hashes"))
                                || cred.contains(QLatin1String("secrets"))))
            return deny(QStringLiteral("deny:credential-write"),
                        QStringLiteral("Blocked: writing %1.").arg(cred));
    }
    // A mid-command cd/pushd/popd/Set-Location moves the cwd that the relative
    // writes above were vetted against, so the base can't be trusted for them:
    // never auto-allow (bypass, session grant, saved rule), ask instead.
    if (paths.changesDir) {
        for (const QString &w : paths.writes) {
            if (!noch::agentpolicy::isAbsoluteShellPath(w))
                return ask(QStringLiteral("ask:cd-relative-write"),
                           QStringLiteral("This command changes directory, so its relative paths "
                                          "cannot be checked against the workspace. Confirm where "
                                          "it writes before it runs."));
        }
    }
    // Paths the command would read: credential stores and Noch private data ask.
    for (const QString &r : paths.reads) {
        if (isPrivatePath(r))
            return ask(QStringLiteral("ask:private-read"),
                       QStringLiteral("Reading Noch's own settings or API key."));
        const QString cred = credentialPathReason(r, base);
        if (!cred.isEmpty())
            return ask(QStringLiteral("ask:sensitive-read"), QStringLiteral("Reading %1.").arg(cred));
    }

    // 2. Opaque commands (eval, encoded, variable-as-program): never auto-allow.
    const QString opaque = opaqueCommandReason(cmd);
    const bool plan = m_mode == Mode::Plan;
    if (writesInstructions) {
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode is read-only; this command could change the system."));
        return ask(QStringLiteral("ask:instructions-file"),
                   QStringLiteral("This changes Buddy's standing instructions (NOCH.md, AGENTS.md, CLAUDE.md or a .noch "
                                  "folder). What is written there is added to every future prompt, so check it."));
    }
    const bool bypass = m_mode == Mode::Bypass && !m_tainted;
    const bool granted = m_sessionAllow.contains(QLatin1String("bash")) && !m_tainted;

    if (!opaque.isEmpty()) {
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode is read-only; this command could change the system."));
        return ask(QStringLiteral("ask:opaque"),
                   QStringLiteral("This command %1, so it needs your approval.").arg(opaque));
    }

    // 3. Read-only fast path (all modes).
    if (isReadOnlyCommand(cmd))
        return allow(QStringLiteral("read-only"));

    // 4. Mode.
    if (plan)
        return deny(QStringLiteral("mode:plan"),
                    QStringLiteral("Plan mode is read-only; this command could change the system."));
    if (bypass)
        return allow(QStringLiteral("mode:bypass"));
    if (granted)
        return allow(QStringLiteral("session-grant"));
    if (!m_tainted && matchesSavedRule(QStringLiteral("bash"), input))
        return allow(QStringLiteral("saved-rule"));
    return ask(QStringLiteral("ask:shell"), QStringLiteral("This command can change files or settings."));
}

AgentPolicy::Decision AgentPolicy::evaluate(const QString &tool, const QJsonObject &input) const
{
    auto allow = [](const QString &rule, const QString &reason = QString()) {
        return Decision{ Verdict::Allow, reason, rule };
    };
    auto deny = [](const QString &rule, const QString &reason) {
        return Decision{ Verdict::Deny, reason, rule };
    };
    auto ask = [](const QString &rule, const QString &reason) {
        return Decision{ Verdict::Ask, reason, rule };
    };
    const bool plan = m_mode == Mode::Plan;
    // Taint (prompt injection seen in this run) suspends Trusted and grants.
    const bool bypass = m_mode == Mode::Bypass && !m_tainted;
    const bool granted = m_sessionAllow.contains(tool) && !m_tainted;

    if (tool == QLatin1String("bash"))
        return evaluateShell(input);

    if (tool == QLatin1String("computer")) {
        // Normalize once, up front: the executor lowercases/trims too, so the
        // gate must judge the same action string the executor will run.
        const QString action = input.value(QStringLiteral("action")).toString().trimmed().toLower();
        if (isObservingComputerAction(action))
            return allow(QStringLiteral("read-only"));
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode cannot click or type on your screen."));
        static const QStringList known{
            "mouse_move", "left_click", "right_click", "middle_click", "double_click", "triple_click",
            "left_click_drag", "scroll", "type", "key", "left_mouse_down", "left_mouse_up", "hold_key" };
        if (!known.contains(action))
            return deny(QStringLiteral("deny:computer-action"),
                        QStringLiteral("Unknown computer action '%1'.").arg(action.left(40)));
        const QString text = input.value(QStringLiteral("text")).toString();
        if (action == QLatin1String("type") && text.size() > 2000)
            return deny(QStringLiteral("deny:type-length"), QStringLiteral("Typing more than 2000 characters at once is blocked."));
        // After reading the screen, a risky shortcut or command-like typing
        // is asked for even when Trusted mode or a grant would allow it.
        // Combos go through the canonical parser, so aliases (win+r,
        // ctrl+alt+del, option+f4, ...) resolve to the same combos the
        // executor sees instead of being matched as raw text.
        if (m_screenTainted || m_tainted) {
            const bool riskyKey = (action == QLatin1String("key") || action == QLatin1String("hold_key"))
                && cu::isRiskyKeyCombo(cu::parseKeyCombo(text));
            const bool shellText = action == QLatin1String("type") && cu::looksLikeShellText(text);
            if (riskyKey || shellText)
                return ask(QStringLiteral("ask:screen-tainted"),
                           riskyKey ? QStringLiteral("After reading the screen, Buddy wants to press %1 (opens system tools).").arg(text.trimmed())
                                    : QStringLiteral("After reading the screen, Buddy wants to type what looks like a command."));
        }
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (granted)
            return allow(QStringLiteral("session-grant"));
        return ask(QStringLiteral("ask:computer"), QStringLiteral("Buddy wants to control the mouse or keyboard."));
    }

    if (tool == QLatin1String("str_replace_editor")) {
        const QString cmd = input.value(QStringLiteral("command")).toString();
        const QString path = input.value(QStringLiteral("path")).toString();
        const QString prot = protectedPathReason(path, m_base);
        if (isPrivatePath(path)) {
            if (isReadOnlyEditorCommand(cmd))
                return bypass ? allow(QStringLiteral("mode:bypass"))
                    : ask(QStringLiteral("ask:private-read"),
                          QStringLiteral("Reading Noch's own settings or API key."));
            return deny(QStringLiteral("deny:private-path"),
                        QStringLiteral("Blocked: Buddy cannot change Noch's own settings or raise its permissions."));
        }
        if (isReadOnlyEditorCommand(cmd)) {
            const QString cred = credentialPathReason(path, m_base);
            // A session grant covers ordinary reads, not secrets: a file that
            // may contain secrets always asks (Trusted mode aside).
            if ((!cred.isEmpty() || !prot.isEmpty()) && !bypass)
                return ask(QStringLiteral("ask:sensitive-read"),
                           QStringLiteral("Reading %1.").arg(!cred.isEmpty() ? cred : prot));
            return allow(QStringLiteral("read-only"));
        }
        if (!prot.isEmpty())
            return deny(QStringLiteral("deny:protected-path"), QStringLiteral("Blocked: %1.").arg(prot));
        if (plan)
            return deny(QStringLiteral("mode:plan"), QStringLiteral("Plan mode is read-only; file edits are disabled."));
        if (isInstructionPath(path))
            return ask(QStringLiteral("ask:instructions-file"),
                       QStringLiteral("This changes Buddy's standing instructions (%1). What is written there is added to "
                                      "every future prompt, so check it.").arg(QFileInfo(path).fileName()));
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (isSecretsFile(path) || !credentialPathReason(path, m_base).isEmpty())
            // A session grant covers ordinary edits, not secrets: reading a
            // file that may contain secrets always asks (Trusted mode aside).
            return ask(QStringLiteral("ask:secrets-file"), QStringLiteral("%1 may contain secrets.").arg(path));
        if (m_mode == Mode::AcceptEdits && insideWorkspace(path))
            return allow(QStringLiteral("mode:acceptEdits"));
        if (granted)
            return allow(QStringLiteral("session-grant"));
        if (!m_tainted && matchesSavedRule(QStringLiteral("str_replace_editor"), input))
            return allow(QStringLiteral("saved-rule"));
        return ask(QStringLiteral("ask:edit"),
                   insideWorkspace(path) ? QStringLiteral("Buddy wants to change a file.")
                                         : QStringLiteral("Buddy wants to change a file outside the workspace folder."));
    }

    if (tool == QLatin1String("web_fetch") || tool == QLatin1String("web_search")) {
        const QString url = input.value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty()) {
            if (!isFetchableUrl(url))
                return deny(QStringLiteral("deny:url-scheme"),
                            QStringLiteral("Blocked: only http(s) URLs can be fetched (%1).").arg(url));
            // Private-network targets always ask, even with a session grant:
            // "always allow web_fetch" must not silently cover cloud-metadata
            // and loopback URLs.
            if (isPrivateUrl(url))
                return ask(QStringLiteral("ask:private-network"),
                           QStringLiteral("%1 points at your local machine or network.").arg(url));
            // Tainted run: even public URLs ask (exfil / follow-on injection).
            if (m_tainted && !granted)
                return ask(QStringLiteral("ask:tainted"),
                           QStringLiteral("A previous tool result looked like a prompt injection; network access needs approval."));
        }
        if (tool == QLatin1String("web_fetch"))
            return allow(QStringLiteral("network-read"));
        return allow(QStringLiteral("safe-tool"));
    }

    if (tool == QLatin1String("glob_grep")) {
        const QString path = input.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            if (isPrivatePath(path))
                return ask(QStringLiteral("ask:private-read"),
                           QStringLiteral("Searching Noch's own settings or data."));
            const QString cred = credentialPathReason(path, m_base);
            if (!cred.isEmpty() && !bypass && !granted)
                return ask(QStringLiteral("ask:sensitive-read"), QStringLiteral("Searching %1.").arg(cred));
        }
        return allow(QStringLiteral("safe-tool"));
    }

    // The music pill. Transport and volume change nothing on disk. Queueing a file
    // or folder reads it, so anything outside the user's own Music folder asks.
    if (tool == QLatin1String("music")) {
        const QString action = input.value(QStringLiteral("action")).toString().trimmed().toLower();
        if (action == QLatin1String("status") || action == QLatin1String("queue"))
            return allow(QStringLiteral("read-only"));
        if (plan)
            return deny(QStringLiteral("mode:plan"), QStringLiteral("Plan mode does not play music."));
        if (action != QLatin1String("add"))
            return allow(QStringLiteral("media"));
        const QString path = input.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty())
            return allow(QStringLiteral("media"));   // the executor rejects an empty path
        if (isPrivatePath(path))
            return deny(QStringLiteral("deny:private-path"),
                        QStringLiteral("Blocked: that is Noch's own settings or data."));
        const QString music = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
        const QString abs = QDir::cleanPath(absolutePath(path));
        const bool inMusic = !music.isEmpty()
            && (abs.compare(music, Qt::CaseInsensitive) == 0
                || abs.startsWith(music + QLatin1Char('/'), Qt::CaseInsensitive));
        if (inMusic && !m_tainted)
            return allow(QStringLiteral("music-folder"));
        return ask(QStringLiteral("ask:music-add"),
                   QStringLiteral("Adding %1 to the music queue.").arg(QFileInfo(abs).fileName().isEmpty() ? abs : QFileInfo(abs).fileName()));
    }

    // Shows the user their own screen in the chat; the image never goes to the model.
    if (tool == QLatin1String("share_screenshot"))
        return allow(QStringLiteral("read-only"));

    // Sandboxed or read-only tools.
    if (tool == QLatin1String("memory") || tool == QLatin1String("memory_search")
            || tool == QLatin1String("todo_write") || tool == QLatin1String("ask_user_question")
            || tool == QLatin1String("tool_search") || tool.startsWith(QLatin1String("tool_search_tool"))
            || tool == QLatin1String("advisor") || tool.startsWith(QLatin1String("advisor_"))
            || tool.startsWith(QLatin1String("transfer_to_")))
        return allow(QStringLiteral("safe-tool"));

    return deny(QStringLiteral("deny:unknown-tool"), QStringLiteral("Unknown tool '%1'.").arg(tool));
}
