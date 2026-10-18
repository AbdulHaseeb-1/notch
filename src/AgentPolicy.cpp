#include "AgentPolicy.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QUrl>

namespace {

using RX = QRegularExpression;
constexpr auto CI = QRegularExpression::CaseInsensitiveOption;

QString normPath(QString p)
{
    p = p.trimmed();
    if ((p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')))
            || (p.startsWith(QLatin1Char('\'')) && p.endsWith(QLatin1Char('\''))))
        p = p.mid(1, p.size() - 2);
    if (p.startsWith(QLatin1Char('~')))
        p = QDir::homePath() + p.mid(1);
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    p = QDir::cleanPath(p);
    return p.toLower();
}

struct DenyRule {
    const char *pattern;
    const char *reason;
};

// Destructive or security-sensitive shell commands. These are denied in
// every mode, like deny rules in the Claude Agent SDK that apply "even in
// bypassPermissions mode". Patterns run on the whitespace-simplified command.
const DenyRule kShellDeny[] = {
    { R"RX((^|[;&|\s])(sudo\s+)?rm\s+(-[a-z]*\s+)*-[a-z]*(r[a-z]*f|f[a-z]*r)[a-z]*\s+(-[a-z-]+\s+)*("?(/|~|\$home|/\*|\*|\.\.?|[a-z]:[\\/]?)"?)(\s|$))RX",
      "recursive force-delete of a root, home or working directory" },
    { R"RX((^|[;&|\s])(rd|rmdir)\s+(/[sq]\s+)+"?[a-z]:\\?"?(\s|$))RX", "recursive delete of a whole drive" },
    { R"RX((^|[;&|\s])(rd|rmdir|del|erase)\s+.*([a-z]:\\windows|[a-z]:\\program files|%systemroot%|%windir%))RX", "deleting Windows system folders" },
    { R"RX(remove-item\b.*-recurse.*(\s|'|")([a-z]:\\?|~|\$home|\$env:(systemroot|windir|userprofile)|[a-z]:\\windows[^\s]*)('|"|\s|$))RX", "recursive delete of a drive, home or system folder" },
    { R"RX(remove-item\b.*(\s|'|")([a-z]:\\?|~|\$home|\$env:userprofile)('|")?\s+.*-recurse)RX", "recursive delete of a drive or home folder" },
    { R"RX((^|[;&|\s])format(\.com)?\s+[a-z]:)RX", "formatting a drive" },
    { R"RX(\b(diskpart|clear-disk|format-volume|initialize-disk|remove-partition|mkfs(\.\w+)?)\b)RX", "disk partitioning or formatting" },
    { R"RX(\bdd\s+.*\bof=/dev/)RX", "raw write to a block device" },
    { R"RX(:\(\)\s*\{\s*:\s*\|\s*:\s*&\s*\}\s*;\s*:)RX", "fork bomb" },
    { R"RX(\b(curl|wget|iwr|invoke-webrequest|irm|invoke-restmethod)\b[^;&]*\|\s*(sh|bash|zsh|iex|invoke-expression|powershell|pwsh|cmd)\b)RX", "download-and-execute pipeline" },
    { R"RX(\b(iex|invoke-expression)\b.*\b(downloadstring|iwr|irm|invoke-webrequest|invoke-restmethod|net\.webclient)\b)RX", "download-and-execute pipeline" },
    { R"RX(\breg(\.exe)?\s+delete\s+hk(lm|ey_local_machine|cr|ey_classes_root))RX", "deleting machine-wide registry keys" },
    { R"RX(\bremove-item\b.*\bhk(lm|cr):)RX", "deleting machine-wide registry keys" },
    { R"RX(\breg(\.exe)?\s+save\s+hk(lm|ey_local_machine)\\(sam|security|system)\b)RX", "dumping credential hives" },
    { R"RX(\b(mimikatz|procdump[^\n]*lsass|sekurlsa)\b)RX", "credential dumping" },
    { R"RX(\b(bcdedit|bootrec)\b)RX", "changing boot configuration" },
    { R"RX(\bvssadmin\b.*\bdelete\b|\bwbadmin\b.*\bdelete\b|\bwmic\b.*shadowcopy.*delete)RX", "deleting backups or shadow copies" },
    { R"RX(\bcipher\s+/w)RX", "wiping free disk space" },
    { R"RX(\bset-mppreference\b.*-disable|\badd-mppreference\b.*-exclusionpath)RX", "weakening Microsoft Defender" },
    { R"RX(\bnetsh\s+(adv)?firewall\b.*\b(state\s+off|disable))RX", "turning off the firewall" },
    { R"RX(\b(takeown|icacls)\b.*([a-z]:\\windows|system32))RX", "taking ownership of system files" },
    { R"RX(\bchmod\s+(-[a-z]*r[a-z]*\s+)?[0-7]*777\s+/(\s|$))RX", "opening permissions on the root filesystem" },
    { R"RX(\bchown\s+-r\b.*\s/(\s|$))RX", "changing ownership of the root filesystem" },
    { R"RX(>\s*/dev/sd[a-z]|>\s*\\\\\.\\physicaldrive)RX", "raw write to a disk" },
};

// Programs whose plain invocation only reads state.
const char *kReadOnlyPrograms[] = {
    "ls", "dir", "pwd", "cd", "echo", "cat", "type", "more", "head", "tail", "wc",
    "whoami", "hostname", "date", "time", "where", "which", "ver", "systeminfo",
    "ipconfig", "ifconfig", "ping", "nslookup", "tasklist", "tree", "uname", "uptime",
    "df", "du", "free", "ps", "env", "printenv", "set", "file", "stat", "sort", "uniq",
    "findstr", "grep", "rg", "fd", "find", "select-string", "get-childitem", "gci",
    "get-content", "gc", "get-location", "gl", "get-process", "gps", "get-date",
    "get-item", "gi", "test-path", "resolve-path", "get-command", "gcm", "get-help",
    "get-service", "gsv", "get-computerinfo", "get-volume", "get-psdrive",
    "measure-object", "select-object", "format-table", "format-list", "out-string",
    "write-output", "write-host", "get-netipaddress", "get-ciminstance", "wmic",
};

// git subcommands that do not change the repository.
const char *kGitReadOnly[] = {
    "status", "log", "diff", "show", "branch", "remote", "rev-parse", "describe",
    "blame", "ls-files", "shortlog", "tag", "config", "grep", "reflog",
};

bool firstTokenReadOnly(const QString &segment)
{
    const QString s = segment.trimmed();
    if (s.isEmpty())
        return true;
    QStringList toks = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString prog = toks.first().toLower();
    // strip a path and an .exe suffix: C:\Windows\System32\where.exe -> where
    prog.replace(QLatin1Char('\\'), QLatin1Char('/'));
    prog = prog.section(QLatin1Char('/'), -1);
    if (prog.endsWith(QLatin1String(".exe")))
        prog.chop(4);

    // "<anything> --version" / "-v" / "--help" only prints.
    if (toks.size() == 2) {
        const QString a = toks.at(1).toLower();
        if (a == QLatin1String("--version") || a == QLatin1String("-v")
                || a == QLatin1String("-version") || a == QLatin1String("--help")
                || a == QLatin1String("/?"))
            return true;
    }
    if (prog == QLatin1String("git")) {
        if (toks.size() < 2)
            return true;
        const QString sub = toks.at(1).toLower();
        for (const char *g : kGitReadOnly) {
            if (sub == QLatin1String(g)) {
                // "git branch -D", "git tag -d", "git config --global x y" write.
                if ((sub == QLatin1String("branch") || sub == QLatin1String("tag")
                     || sub == QLatin1String("remote") || sub == QLatin1String("config"))
                        && toks.size() > 2) {
                    const QString a = toks.at(2).toLower();
                    if (sub == QLatin1String("config"))
                        return a == QLatin1String("--get") || a == QLatin1String("--list")
                            || a == QLatin1String("-l") || a == QLatin1String("--get-all");
                    if (sub == QLatin1String("remote"))
                        return a == QLatin1String("-v") || a == QLatin1String("show");
                    return a == QLatin1String("--list") || a == QLatin1String("-l")
                        || a == QLatin1String("-a") || a == QLatin1String("-r")
                        || a == QLatin1String("-v") || a == QLatin1String("-vv");
                }
                return true;
            }
        }
        return false;
    }
    if (prog == QLatin1String("find")) {
        static const RX findWrite(QStringLiteral(R"RX(\s-(delete|exec|execdir|ok|fprint)\b)RX"), CI);
        return !findWrite.match(s).hasMatch();
    }
    if (prog == QLatin1String("wmic")) {
        static const RX wmicWrite(QStringLiteral(R"RX(\b(call|delete|set|create)\b)RX"), CI);
        return !wmicWrite.match(s).hasMatch();
    }
    for (const char *p : kReadOnlyPrograms)
        if (prog == QLatin1String(p))
            return true;
    return false;
}

const char *kProtectedDirs[] = {
    "c:/windows", "c:/program files", "c:/program files (x86)", "c:/programdata/microsoft",
    "c:/boot", "c:/recovery", "/etc", "/usr", "/bin", "/sbin", "/boot", "/lib", "/system",
    "/library", "/private/etc",
};

} // namespace

AgentPolicy::Mode AgentPolicy::modeFromString(const QString &s)
{
    const QString k = s.trimmed().toLower();
    if (k == QLatin1String("acceptedits") || k == QLatin1String("accept_edits") || k == QLatin1String("edits"))
        return Mode::AcceptEdits;
    if (k == QLatin1String("plan") || k == QLatin1String("readonly") || k == QLatin1String("read-only"))
        return Mode::Plan;
    if (k == QLatin1String("bypass") || k == QLatin1String("bypasspermissions") || k == QLatin1String("trusted"))
        return Mode::Bypass;
    return Mode::Default;
}

QString AgentPolicy::modeToString(Mode m)
{
    switch (m) {
    case Mode::AcceptEdits: return QStringLiteral("acceptEdits");
    case Mode::Plan: return QStringLiteral("plan");
    case Mode::Bypass: return QStringLiteral("bypass");
    case Mode::Default: break;
    }
    return QStringLiteral("default");
}

void AgentPolicy::setWorkspaceRoots(const QStringList &roots)
{
    m_roots.clear();
    for (const QString &r : roots) {
        const QString n = normPath(r);
        if (!n.isEmpty() && n != QLatin1String("/") && !n.endsWith(QLatin1Char(':')))
            m_roots << n;
    }
}

QString AgentPolicy::dangerousCommandReason(const QString &command)
{
    // Compiled once (function-local static init is thread-safe).
    static const QList<QPair<RX, QString>> rules = [] {
        QList<QPair<RX, QString>> list;
        for (const DenyRule &r : kShellDeny) {
            RX re(QString::fromUtf8(r.pattern), CI);
            if (!re.isValid())
                qWarning("AgentPolicy: invalid deny pattern for '%s': %s", r.reason,
                         qPrintable(re.errorString()));
            list.append({ re, QString::fromUtf8(r.reason) });
        }
        return list;
    }();
    const QString c = command.simplified();
    for (const auto &r : rules)
        if (r.first.match(c).hasMatch())
            return r.second;
    return QString();
}

bool AgentPolicy::isReadOnlyCommand(const QString &command)
{
    const QString c = command.simplified();
    if (c.isEmpty())
        return false;
    // Redirection, command substitution and here-strings can write or run
    // arbitrary code, so they disqualify a command from the read-only fast path.
    static const RX unsafe(QStringLiteral(R"RX((>|`|\$\(|<<|\bset-content\b|\badd-content\b|\bout-file\b|\btee\b|\bstart-process\b|\binvoke-))RX"), CI);
    if (unsafe.match(c).hasMatch())
        return false;
    static const RX splitter(QStringLiteral(R"RX(\s*(?:&&|\|\||;|\||&)\s*)RX"));
    const QStringList segments = c.split(splitter, Qt::SkipEmptyParts);
    for (const QString &seg : segments)
        if (!firstTokenReadOnly(seg))
            return false;
    return !segments.isEmpty();
}

QString AgentPolicy::protectedPathReason(const QString &path)
{
    const QString p = normPath(path);
    if (p.isEmpty())
        return QString();
    for (const char *d : kProtectedDirs) {
        const QString dir = QLatin1String(d);
        if (p == dir || p.startsWith(dir + QLatin1Char('/')))
            return QStringLiteral("%1 is a protected system location").arg(path);
    }
    const QString home = normPath(QDir::homePath());
    for (const char *sub : { "/.ssh", "/.gnupg", "/.aws", "/.azure", "/.kube", "/.docker/config.json",
                             "/appdata/roaming/microsoft/credentials",
                             "/appdata/local/microsoft/credentials" }) {
        const QString dir = home + QLatin1String(sub);
        if (p == dir || p.startsWith(dir + QLatin1Char('/')))
            return QStringLiteral("%1 holds credentials").arg(path);
    }
    if (p.contains(QLatin1String("/.git/hooks/")) || p.endsWith(QLatin1String("/.git/config")))
        return QStringLiteral("%1 can run code on the next git command").arg(path);
    return QString();
}

bool AgentPolicy::insideWorkspace(const QString &path) const
{
    const QString p = normPath(path);
    if (p.isEmpty() || QFileInfo(path).isRelative())
        return false;
    for (const QString &root : m_roots)
        if (p == root || p.startsWith(root + QLatin1Char('/')))
            return true;
    return false;
}

bool AgentPolicy::isReadOnlyEditorCommand(const QString &command)
{
    const QString c = command.trimmed().toLower();
    return c.isEmpty() || c == QLatin1String("view");
}

bool AgentPolicy::isObservingComputerAction(const QString &action)
{
    const QString a = action.trimmed().toLower();
    return a == QLatin1String("screenshot") || a == QLatin1String("cursor_position")
        || a == QLatin1String("wait");
}

static bool sensitiveFile(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    return name == QLatin1String(".env") || name.startsWith(QLatin1String(".env."))
        || name.endsWith(QLatin1String(".pem")) || name.endsWith(QLatin1String(".key"))
        || name.endsWith(QLatin1String(".pfx")) || name.startsWith(QLatin1String("id_rsa"))
        || name.startsWith(QLatin1String("id_ed25519"));
}

static bool privateHost(const QString &rawUrl)
{
    const QUrl u = QUrl::fromUserInput(rawUrl);
    if (u.scheme() == QLatin1String("file"))
        return true;
    const QString h = u.host().toLower();
    if (h.isEmpty())
        return false;
    static const RX priv(QStringLiteral(R"RX(^(localhost|.*\.local|.*\.internal|127\.|10\.|192\.168\.|169\.254\.|172\.(1[6-9]|2\d|3[01])\.|0\.0\.0\.0|\[?::1\]?$|\[?fe80:|\[?fc|\[?fd))RX"));
    return priv.match(h).hasMatch();
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
    const bool bypass = m_mode == Mode::Bypass;
    const bool granted = m_sessionAllow.contains(tool);

    if (tool == QLatin1String("bash")) {
        const QString cmd = input.value(QStringLiteral("command")).toString();
        // 1. guardrail hook + deny rules: never run, in any mode.
        const QString danger = dangerousCommandReason(cmd);
        if (!danger.isEmpty())
            return deny(QStringLiteral("deny:shell"), QStringLiteral("Blocked: %1.").arg(danger));
        // 2. read-only commands are approved on their own (all modes).
        if (isReadOnlyCommand(cmd))
            return allow(QStringLiteral("read-only"));
        // 3. mode
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode is read-only; this command could change the system."));
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (granted)
            return allow(QStringLiteral("session-grant"));
        return ask(QStringLiteral("ask:shell"), QStringLiteral("This command can change files or settings."));
    }

    if (tool == QLatin1String("computer")) {
        const QString action = input.value(QStringLiteral("action")).toString();
        if (isObservingComputerAction(action))
            return allow(QStringLiteral("read-only"));
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode cannot click or type on your screen."));
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (granted)
            return allow(QStringLiteral("session-grant"));
        return ask(QStringLiteral("ask:computer"), QStringLiteral("Buddy wants to control the mouse or keyboard."));
    }

    if (tool == QLatin1String("str_replace_editor")) {
        const QString cmd = input.value(QStringLiteral("command")).toString();
        const QString path = input.value(QStringLiteral("path")).toString();
        const QString prot = protectedPathReason(path);
        if (isReadOnlyEditorCommand(cmd)) {
            if (!prot.isEmpty() && !bypass && !granted)
                return ask(QStringLiteral("ask:sensitive-read"), QStringLiteral("Reading %1.").arg(prot));
            return allow(QStringLiteral("read-only"));
        }
        if (!prot.isEmpty())
            return deny(QStringLiteral("deny:protected-path"), QStringLiteral("Blocked: %1.").arg(prot));
        if (plan)
            return deny(QStringLiteral("mode:plan"), QStringLiteral("Plan mode is read-only; file edits are disabled."));
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (sensitiveFile(path))
            return granted ? allow(QStringLiteral("session-grant"))
                           : ask(QStringLiteral("ask:secrets-file"), QStringLiteral("%1 may contain secrets.").arg(path));
        if (m_mode == Mode::AcceptEdits && insideWorkspace(path))
            return allow(QStringLiteral("mode:acceptEdits"));
        if (granted)
            return allow(QStringLiteral("session-grant"));
        return ask(QStringLiteral("ask:edit"),
                   insideWorkspace(path) ? QStringLiteral("Buddy wants to change a file.")
                                         : QStringLiteral("Buddy wants to change a file outside the workspace folder."));
    }

    if (tool == QLatin1String("web_fetch")) {
        const QString url = input.value(QStringLiteral("url")).toString();
        if (privateHost(url) && !bypass && !granted)
            return ask(QStringLiteral("ask:private-network"),
                       QStringLiteral("%1 points at your local machine or network.").arg(url));
        return allow(QStringLiteral("network-read"));
    }

    // Sandboxed or read-only tools.
    if (tool == QLatin1String("memory") || tool == QLatin1String("glob_grep")
            || tool == QLatin1String("web_search") || tool == QLatin1String("todo_write")
            || tool == QLatin1String("tool_search") || tool.startsWith(QLatin1String("tool_search_tool"))
            || tool == QLatin1String("advisor") || tool.startsWith(QLatin1String("advisor_")))
        return allow(QStringLiteral("safe-tool"));

    return deny(QStringLiteral("deny:unknown-tool"), QStringLiteral("Unknown tool '%1'.").arg(tool));
}

QString AgentPolicy::redactSecrets(const QString &text, const QStringList &knownSecrets)
{
    if (text.isEmpty())
        return text;
    QString out = text;
    for (const QString &s : knownSecrets) {
        if (s.trimmed().size() >= 8)
            out.replace(s.trimmed(), QStringLiteral("[redacted]"));
    }
    static const RX patterns[] = {
        RX(QStringLiteral(R"RX(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)RX")),
        RX(QStringLiteral(R"RX(\bsk-(?:ant-|proj-)?[A-Za-z0-9_\-]{20,})RX")),
        RX(QStringLiteral(R"RX(\bAKIA[0-9A-Z]{16}\b)RX")),
        RX(QStringLiteral(R"RX(\bgh[pousr]_[A-Za-z0-9]{30,})RX")),
        RX(QStringLiteral(R"RX(\bgithub_pat_[A-Za-z0-9_]{40,})RX")),
        RX(QStringLiteral(R"RX(\bxox[abprs]-[A-Za-z0-9\-]{10,})RX")),
        RX(QStringLiteral(R"RX(\bAIza[0-9A-Za-z_\-]{35}\b)RX")),
        RX(QStringLiteral(R"RX(\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,})RX")),
    };
    for (const RX &re : patterns)
        out.replace(re, QStringLiteral("[redacted]"));
    static const RX kv(QStringLiteral(R"RX(((?:api[_-]?key|secret|password|passwd|access[_-]?token|auth[_-]?token|client[_-]?secret)\s*[:=]\s*["']?)([^\s"',;]{8,}))RX"), CI);
    out.replace(kv, QStringLiteral("\\1[redacted]"));
    return out;
}

bool AgentPolicy::looksLikePromptInjection(const QString &text)
{
    static const RX re(QStringLiteral(
        R"RX(ignore\s+(all\s+|any\s+|the\s+)?(previous|prior|above|earlier)\s+(instructions|prompts|messages)|)RX"
        R"RX(disregard\s+(the\s+|your\s+)?(system|previous|prior)\s+(prompt|instructions)|)RX"
        R"RX(you\s+are\s+now\s+(in\s+)?(developer|dan|jailbreak)|)RX"
        R"RX(<\|im_start\|>|\[\s*system\s*\]\s*:|new\s+system\s+prompt|)RX"
        R"RX((send|post|upload|exfiltrate)\s+(your|the|all)\s+(api\s*key|credentials|secrets|tokens|passwords))RX"), CI);
    return re.match(text).hasMatch();
}
