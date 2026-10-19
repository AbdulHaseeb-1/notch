#include "AgentPolicyInternal.h"

#include <QDebug>
#include <QDir>
#include <QRegularExpression>

using namespace noch::agentpolicy;

namespace {

// ---- shell -----------------------------------------------------------------

struct DenyRule {
    const char *pattern;
    const char *reason;
};

// Destructive or security-sensitive shell commands. These are denied in
// every mode, like deny rules in the Claude Agent SDK that apply "even in
// bypassPermissions mode". Patterns run on the whitespace-simplified command
// and on a de-obfuscated copy (quotes, escapes and carets removed).
// Recursive deletes are checked structurally in catastrophicDelete().
const DenyRule kShellDeny[] = {
    { R"RX((^|[;&|\s(`{])(sudo\s+)?rm\s+(-[a-z]*\s+)*-[a-z]*(r[a-z]*f|f[a-z]*r)[a-z]*\s+(-[a-z-]+\s+)*("?(/|~|\$home|/\*|\*|\.\.?|[a-z]:[\\/]?)"?)(\s|$|\)))RX",
      "recursive force-delete of a root, home or working directory" },
    { R"RX((^|[;&|\s])(rd|rmdir)\s+(/[sq]\s+)+"?[a-z]:\\?"?(\s|$))RX", "recursive delete of a whole drive" },
    { R"RX((^|[;&|\s])(rd|rmdir|del|erase)\s+.*([a-z]:\\windows|[a-z]:\\program files|%systemroot%|%windir%))RX", "deleting Windows system folders" },
    { R"RX(remove-item\b.*-recurse.*(\s|'|")([a-z]:\\?|~|\$home|\$env:(systemroot|windir|userprofile)|[a-z]:\\windows[^\s]*)('|"|\s|$))RX", "recursive delete of a drive, home or system folder" },
    { R"RX(remove-item\b.*(\s|'|")([a-z]:\\?|~|\$home|\$env:userprofile)('|")?\s+.*-recurse)RX", "recursive delete of a drive or home folder" },
    { R"RX((^|[;&|\s])format(\.com)?\s+[a-z]:)RX", "formatting a drive" },
    { R"RX(\b(diskpart|clear-disk|format-volume|initialize-disk|remove-partition|mkfs(\.\w+)?|wipefs|sgdisk|fdisk|parted)\b)RX", "disk partitioning or formatting" },
    { R"RX(\bdd\s+.*\bof=/dev/)RX", "raw write to a block device" },
    { R"RX(:\(\)\s*\{\s*:\s*\|\s*:\s*&\s*\}\s*;\s*:)RX", "fork bomb" },
    { R"RX(\b(curl|wget|iwr|invoke-webrequest|irm|invoke-restmethod)\b[^;&]*\|\s*(sh|bash|zsh|iex|invoke-expression|powershell|pwsh|cmd)\b)RX", "download-and-execute pipeline" },
    { R"RX(\b(iex|invoke-expression)\b.*\b(downloadstring|downloadfile|iwr|irm|invoke-webrequest|invoke-restmethod|net\.webclient)\b)RX", "download-and-execute pipeline" },
    { R"RX(\|\s*(sudo\s+)?(sh|bash|zsh|dash|ksh|fish|pwsh|powershell|cmd)(\.exe)?(\s+-[a-z]+)*\s*(-\s*)?($|[;&|)]))RX", "piping generated text into a shell" },
    { R"RX(\|\s*(iex|invoke-expression)\b)RX", "piping generated text into a shell" },
    { R"RX(\b(frombase64string|base64\s+(-d|--decode|-D))\b.*\b(iex|invoke-expression|sh|bash|scriptblock\]::create)\b)RX", "decoding and running a hidden command" },
    { R"RX(\breg(\.exe)?\s+delete\s+hk(lm|ey_local_machine|cr|ey_classes_root))RX", "deleting machine-wide registry keys" },
    { R"RX(\bremove-item\b.*\bhk(lm|cr):)RX", "deleting machine-wide registry keys" },
    { R"RX(\breg(\.exe)?\s+save\s+hk(lm|ey_local_machine)\\(sam|security|system)\b)RX", "dumping credential hives" },
    { R"RX(\b(mimikatz|procdump[^\n]*lsass|sekurlsa|comsvcs(\.dll)?[^\n]*minidump)\b)RX", "credential dumping" },
    { R"RX(\b(bcdedit|bootrec)\b)RX", "changing boot configuration" },
    { R"RX(\bvssadmin\b.*\bdelete\b|\bwbadmin\b.*\bdelete\b|\bwmic\b.*shadowcopy.*delete)RX", "deleting backups or shadow copies" },
    { R"RX(\bcipher\s+/w)RX", "wiping free disk space" },
    { R"RX(\bset-mppreference\b.*-disable|\badd-mppreference\b.*-exclusion(path|process|extension))RX", "weakening Microsoft Defender" },
    { R"RX(\bnetsh\s+(adv)?firewall\b.*\b(state\s+off|disable)|\bset-netfirewallprofile\b.*-enabled\s+(false|\$false|0))RX", "turning off the firewall" },
    { R"RX(\b(takeown|icacls)\b.*([a-z]:\\windows|system32))RX", "taking ownership of system files" },
    { R"RX(\bchmod\s+(-[a-z]*r[a-z]*\s+)?[0-7]*777\s+/(\s|$))RX", "opening permissions on the root filesystem" },
    { R"RX(\bchown\s+-r\b.*\s/(\s|$))RX", "changing ownership of the root filesystem" },
    { R"RX(>\s*/dev/(sd[a-z]|nvme\d|disk\d)|>\s*\\\\\.\\physicaldrive)RX", "raw write to a disk" },
    { R"RX(\\currentversion\\(run|runonce)\b.*\b(reg(\.exe)?\s+add|new-itemproperty|set-itemproperty)|\b(reg(\.exe)?\s+add|new-itemproperty|set-itemproperty)\b.*\\currentversion\\(run|runonce)\b)RX", "adding a program to Windows autostart" },
    { R"RX(\\start menu\\programs\\startup\b)RX", "writing to the Startup folder" },
    { R"RX(\benablelua\b|\bconsentpromptbehavioradmin\b)RX", "turning off User Account Control" },
};

const char *kReadOnlyPrograms[] = {
    "ls", "dir", "pwd", "cd", "echo", "cat", "type", "more", "head", "tail", "wc",
    "whoami", "date", "where", "which", "ver", "systeminfo",
    "ipconfig", "ping", "nslookup", "tasklist", "tree", "uname", "uptime",
    "df", "du", "free", "ps", "env", "printenv", "file", "stat", "sort", "uniq",
    "findstr", "grep", "rg", "fd", "find", "select-string", "get-childitem", "gci",
    "get-content", "gc", "get-location", "gl", "get-process", "gps", "get-date",
    "get-item", "gi", "test-path", "resolve-path", "get-command", "gcm", "get-help",
    "get-service", "gsv", "get-computerinfo", "get-volume", "get-psdrive",
    "measure-object", "select-object", "format-table", "format-list", "out-string",
    "write-output", "write-host", "get-netipaddress", "get-ciminstance", "wmic", "hostname",
};

const char *kWrapperPrograms[] = {
    "sudo", "doas", "env", "command", "builtin", "exec", "nohup", "nice", "ionice", "time",
    "timeout", "stdbuf", "xargs", "call", "start", "runas", "chroot", "busybox", "strace", "watch",
};

const char *kWriterPrograms[] = {
    "cp", "copy", "copy-item", "cpi", "xcopy", "robocopy", "mv", "move", "move-item", "mi", "ren",
    "rename", "rename-item", "rni", "rm", "del", "erase", "rd", "rmdir", "ri", "remove-item", "rimraf",
    "mkdir", "md", "new-item", "ni", "touch", "tee", "tee-object", "set-content", "sc", "add-content",
    "ac", "out-file", "ln", "chmod", "chown", "chgrp", "icacls", "takeown", "attrib", "install",
    "truncate", "shred", "unlink", "rsync", "scp", "tar", "unzip", "expand-archive", "7z", "sed",
    "perl", "dd", "curl", "wget", "invoke-webrequest", "iwr", "set-acl", "clear-content", "clc",
    "set-itemproperty", "sp", "new-itemproperty", "reg", "git", "npm", "pip", "python", "python3", "node",
};

const char *kDeletePrograms[] = { "rm", "rimraf", "del", "erase", "rd", "rmdir", "ri", "remove-item", "shred" };
const char *kShells[] = { "sh", "bash", "zsh", "dash", "ksh", "fish", "cmd", "powershell", "pwsh" };

bool inList(const QString &v, const char *const *list, int n)
{
    for (int i = 0; i < n; ++i)
        if (v == QLatin1String(list[i]))
            return true;
    return false;
}
template <int N> bool inList(const QString &v, const char *const (&list)[N]) { return inList(v, list, N); }

QString progName(const QString &word)
{
    QString p = word.toLower();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    p = p.section(QLatin1Char('/'), -1);
    for (const char *ext : { ".exe", ".com", ".cmd", ".bat", ".ps1" })
        if (p.endsWith(QLatin1String(ext)))
            p.chop(int(qstrlen(ext)));
    return p;
}

struct Seg {
    QStringList argv;
    QStringList outs;
    QStringList ins;
    int pipeline = 0;
};

struct Parsed {
    QList<Seg> segs;
    bool substitution = false;
};

QString preNormalize(QString s)
{
    static const RX ifs(QStringLiteral(R"(\$\{?IFS\}?)"));
    s.replace(ifs, QStringLiteral(" "));
    static const RX braceVar(QStringLiteral(R"(\$\{(\w+(?::\w+)?)\})"));
    s.replace(braceVar, QStringLiteral("$\\1"));
    // bash brace expansion without spaces: {rm,-rf,/} -> rm -rf /
    static const RX braceList(QStringLiteral(R"((?<![$@\w])\{([^{}\s;=$'"]+(?:,[^{}\s;=$'"]*)+)\})"));
    QString out;
    int last = 0;
    auto it = braceList.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        QString inner = m.captured(1);
        inner.replace(QLatin1Char(','), QLatin1Char(' '));
        out += inner;
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

void parseShell(const QString &raw, Parsed &out, int depth);

void addNested(const QString &inner, Parsed &out, int depth)
{
    out.substitution = true;
    if (!inner.trimmed().isEmpty())
        parseShell(inner, out, depth + 1);
}

// A small shell lexer: enough of sh, cmd and PowerShell quoting to see the
// real program names and arguments. It over-approximates on purpose: text
// inside $( ), ( ), { } and backticks is also parsed as commands.
void parseShell(const QString &raw, Parsed &out, int depth)
{
    if (depth > 4)
        return;
    const QString s = preNormalize(raw);
    Seg cur;
    QString word;
    int pipeId = 0;
    auto flushWord = [&] {
        if (!word.isEmpty()) {
            cur.argv << word;
            word.clear();
        }
    };
    auto flushSeg = [&] {
        flushWord();
        if (!cur.argv.isEmpty() || !cur.outs.isEmpty() || !cur.ins.isEmpty()) {
            cur.pipeline = pipeId;
            out.segs.append(cur);
        }
        cur = Seg();
    };
    auto skipSpace = [&](int &i) {
        while (i < s.size() && s.at(i).isSpace())
            ++i;
    };
    auto takeBalanced = [&](int &i, QChar open, QChar close) -> QString {
        int depth2 = 0;
        const int start = i;
        for (; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (c == QLatin1Char('\\') && i + 1 < s.size()) { ++i; continue; }
            if (c == open) ++depth2;
            else if (c == close) {
                if (--depth2 == 0) { ++i; return s.mid(start + 1, i - start - 2); }
            }
        }
        return s.mid(start + 1);
    };

    for (int i = 0; i < s.size(); ) {
        QChar c = s.at(i);
        if (c.isSpace()) { flushWord(); ++i; continue; }
        // cmd.exe caret escape: ^& ^| ^>  and  ^"foo"^
        if (c == QLatin1Char('^') && i + 1 < s.size()) {
            word += s.at(i + 1);
            i += 2;
            continue;
        }
        // Backslash is an escape only before a shell metacharacter or another
        // backslash. Otherwise keep it so Windows paths (C:\Windows\...) stay intact.
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar n = s.at(i + 1);
            const bool meta = n == QLatin1Char(' ') || n == QLatin1Char('\\')
                || n == QLatin1Char('"') || n == QLatin1Char('\'')
                || n == QLatin1Char('`') || n == QLatin1Char('$')
                || n == QLatin1Char('(') || n == QLatin1Char(')')
                || n == QLatin1Char('{') || n == QLatin1Char('}')
                || n == QLatin1Char(';') || n == QLatin1Char('|')
                || n == QLatin1Char('&') || n == QLatin1Char('<')
                || n == QLatin1Char('>') || n == QLatin1Char('\n')
                || n == QLatin1Char('\t');
            if (meta) {
                word += n;
                i += 2;
            } else {
                word += c;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar q = c;
            ++i;
            while (i < s.size() && s.at(i) != q) {
                if (s.at(i) == QLatin1Char('\\') && q == QLatin1Char('"') && i + 1 < s.size()) {
                    word += s.at(i + 1);
                    i += 2;
                } else {
                    word += s.at(i++);
                }
            }
            if (i < s.size())
                ++i;
            continue;
        }
        // PowerShell here-string @"..."@ / @'...'@
        if (c == QLatin1Char('@') && i + 1 < s.size()
                && (s.at(i + 1) == QLatin1Char('"') || s.at(i + 1) == QLatin1Char('\''))) {
            const QChar q = s.at(i + 1);
            i += 2;
            const QString endTok = QString(q) + QLatin1Char('@');
            const int endAt = s.indexOf(endTok, i);
            if (endAt < 0) { word += s.mid(i); break; }
            word += s.mid(i, endAt - i);
            i = endAt + 2;
            continue;
        }
        if (c == QLatin1Char('`')) {   // backtick command substitution
            ++i;
            const int endB = s.indexOf(QLatin1Char('`'), i);
            if (endB < 0) { word += s.mid(i); break; }
            addNested(s.mid(i, endB - i), out, depth);
            word += QLatin1String("$()");
            i = endB + 1;
            continue;
        }
        if (c == QLatin1Char('$') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('(')) {
            i += 1;
            addNested(takeBalanced(i, QLatin1Char('('), QLatin1Char(')')), out, depth);
            word += QLatin1String("$()");
            continue;
        }
        if (c == QLatin1Char('<') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('(')) {
            i += 1;
            addNested(takeBalanced(i, QLatin1Char('('), QLatin1Char(')')), out, depth);
            word += QLatin1String("$()");
            continue;
        }
        // Redirection: > >> >| 2> &> < <<
        if (c == QLatin1Char('>') || c == QLatin1Char('<')
                || (c.isDigit() && i + 1 < s.size() && (s.at(i + 1) == QLatin1Char('>') || s.at(i + 1) == QLatin1Char('<')))) {
            flushWord();
            const bool isOut = (c == QLatin1Char('>') || (c.isDigit() && s.at(i + 1) == QLatin1Char('>')));
            while (i < s.size() && (s.at(i) == QLatin1Char('>') || s.at(i) == QLatin1Char('<')
                                    || s.at(i) == QLatin1Char('&') || s.at(i) == QLatin1Char('|')
                                    || s.at(i).isDigit()))
                ++i;
            skipSpace(i);
            QString target;
            while (i < s.size() && !s.at(i).isSpace()
                   && s.at(i) != QLatin1Char(';') && s.at(i) != QLatin1Char('|')
                   && s.at(i) != QLatin1Char('&') && s.at(i) != QLatin1Char('>')) {
                if (s.at(i) == QLatin1Char('"') || s.at(i) == QLatin1Char('\'')) {
                    const QChar q = s.at(i++);
                    while (i < s.size() && s.at(i) != q)
                        target += s.at(i++);
                    if (i < s.size()) ++i;
                } else {
                    target += s.at(i++);
                }
            }
            if (!target.isEmpty()) {
                if (isOut) cur.outs << target;
                else cur.ins << target;
            }
            continue;
        }
        // Separators: ; && || & | \n
        if (c == QLatin1Char(';') || c == QLatin1Char('\n')
                || (c == QLatin1Char('&') && (i + 1 >= s.size() || s.at(i + 1) != QLatin1Char('&')))
                || (c == QLatin1Char('&') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('&'))
                || (c == QLatin1Char('|') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('|'))) {
            flushSeg();
            ++pipeId;
            if (c == QLatin1Char('&') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('&'))
                i += 2;
            else if (c == QLatin1Char('|') && i + 1 < s.size() && s.at(i + 1) == QLatin1Char('|'))
                i += 2;
            else
                ++i;
            continue;
        }
        if (c == QLatin1Char('|')) {
            flushSeg();
            ++i;
            continue;   // same pipeline id
        }
        if (c == QLatin1Char('(') || c == QLatin1Char('{')) {
            const QChar close = c == QLatin1Char('(') ? QLatin1Char(')') : QLatin1Char('}');
            addNested(takeBalanced(i, c, close), out, depth);
            word += QLatin1String("$()");
            continue;
        }
        word += c;
        ++i;
    }
    flushSeg();
}


bool looksRecursive(const QStringList &argv)
{
    for (const QString &a : argv) {
        const QString l = a.toLower();
        if (l == QLatin1String("-r") || l == QLatin1String("-rf") || l == QLatin1String("-fr")
                || l == QLatin1String("-rfr") || l == QLatin1String("--recursive")
                || l == QLatin1String("/s") || l == QLatin1String("-recurse")
                || l == QLatin1String("-force")  // PowerShell Remove-Item -Force alone is still risky with -Recurse
                || (l.startsWith(QLatin1Char('-')) && l.contains(QLatin1Char('r'))
                    && (l.contains(QLatin1Char('f')) || l == QLatin1String("-r")
                        || l.contains(QLatin1String("recursive")))))
            return true;
        if (l == QLatin1String("/s") || l.startsWith(QLatin1String("/s/")))
            return true;
    }
    // Combined short flags: -rf, -fr, -R, --force (with Remove-Item -Recurse handled above)
    for (const QString &a : argv) {
        if (!a.startsWith(QLatin1Char('-')) || a.startsWith(QLatin1String("--")))
            continue;
        if (a.contains(QLatin1Char('r')) || a.contains(QLatin1Char('R')))
            return true;
    }
    return false;
}

bool isCatastrophicTarget(const QString &raw)
{
    QString t = expandVars(unquote(raw)).trimmed();
    t.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (t.endsWith(QLatin1Char('/')) && t.size() > 1)
        t.chop(1);
    const QString l = t.toLower();
    if (l.isEmpty() || l == QLatin1String(".") || l == QLatin1String("..")
            || l == QLatin1String("*") || l == QLatin1String("/*") || l == QStringLiteral(R"(\*)")
            || l == QLatin1String("/") || l == QLatin1String("~") || l == QLatin1String("$home")
            || l == QLatin1String("${home}") || l == QLatin1String("$env:userprofile")
            || l == QLatin1String("%userprofile%") || l == QLatin1String("%homepath%"))
        return true;
    static const RX driveRoot(QStringLiteral(R"(^[a-z]:[/\\]?$)"), CI);
    if (driveRoot.match(l).hasMatch())
        return true;
    // Home itself (after expansion).
    const QString home = QDir::fromNativeSeparators(QDir::homePath()).toLower();
    if (l == home || l == home + QLatin1Char('/'))
        return true;
    return false;
}

QString catastrophicDelete(const Parsed &parsed)
{
    for (const Seg &seg : parsed.segs) {
        if (seg.argv.isEmpty())
            continue;
        // Unwrap sudo / env / command ...
        int i = 0;
        while (i < seg.argv.size() && inList(progName(seg.argv.at(i)), kWrapperPrograms)) {
            const QString p = progName(seg.argv.at(i));
            ++i;
            if (p == QLatin1String("env") || p == QLatin1String("sudo") || p == QLatin1String("doas")) {
                while (i < seg.argv.size() && (seg.argv.at(i).contains(QLatin1Char('='))
                                               || seg.argv.at(i).startsWith(QLatin1Char('-'))))
                    ++i;
            }
        }
        if (i >= seg.argv.size())
            continue;
        QString prog = progName(seg.argv.at(i));
        QStringList args = seg.argv.mid(i + 1);
        // Quote-glued flags: rm"-rf" tokenises as the single word "rm-rf".
        if (prog.startsWith(QLatin1String("rm-")) || prog.startsWith(QLatin1String("rm/"))) {
            args.prepend(QLatin1Char('-') + prog.mid(2));
            prog = QStringLiteral("rm");
        }
        if (!inList(prog, kDeletePrograms))
            continue;
        const bool recursive = looksRecursive(args)
            || prog == QLatin1String("rimraf")
            || (prog == QLatin1String("remove-item") && looksRecursive(args));
        if (!recursive && prog != QLatin1String("rimraf"))
            continue;
        for (const QString &a : args) {
            if (a.startsWith(QLatin1Char('-')))
                continue;
            // DOS short flags (/s /q /f), not unix paths.
            if (a.size() <= 3 && a.startsWith(QLatin1Char('/'))
                    && a.indexOf(QLatin1Char('/'), 1) < 0 && a != QLatin1String("/"))
                continue;
            if (isCatastrophicTarget(a))
                return QStringLiteral("recursive force-delete of a root, home or working directory");
        }
        // find ... -delete / -exec rm
        if (prog == QLatin1String("find")) {
            for (const QString &a : args)
                if (a == QLatin1String("-delete") || a == QLatin1String("-exec")
                        || a == QLatin1String("-execdir") || a == QLatin1String("-ok"))
                    return QString();  // handled as write, not hard-deny unless target is /
        }
    }
    // find / -delete
    for (const Seg &seg : parsed.segs) {
        if (seg.argv.isEmpty() || progName(seg.argv.first()) != QLatin1String("find"))
            continue;
        bool del = false;
        QString target;
        for (int i = 1; i < seg.argv.size(); ++i) {
            const QString &a = seg.argv.at(i);
            if (a == QLatin1String("-delete"))
                del = true;
            else if (!a.startsWith(QLatin1Char('-')) && target.isEmpty())
                target = a;
        }
        if (del && isCatastrophicTarget(target.isEmpty() ? QStringLiteral(".") : target))
            return QStringLiteral("recursive force-delete of a root, home or working directory");
    }
    return QString();
}

QString opaqueReason(const Parsed &parsed, const QString &raw)
{
    static const RX evalish(QStringLiteral(
        R"RX(\b(eval|invoke-expression|iex|scriptblock\]::create|frombase64string|\[convert\]::frombase64string)\b)RX"
        R"RX(|\b(bash|sh|zsh|dash|ksh|fish|pwsh|powershell|cmd)(\.exe)?\s+(-[a-z]*c|--command|/c)\b)RX"
        R"RX(|\b(python|python3|perl|ruby|node|php|lua|osascript)\b[^\n]*\s(-c|--eval|-e)\b)RX"
        R"RX(|\b(mshta|rundll32|regsvr32|certutil)\b)RX"), CI);
    if (evalish.match(raw).hasMatch())
        return QStringLiteral("runs a command the gate cannot see");
    for (const Seg &seg : parsed.segs) {
        if (seg.argv.isEmpty())
            continue;
        const QString p = progName(seg.argv.first());
        if (inList(p, kShells) && seg.argv.size() >= 2) {
            for (const QString &a : seg.argv.mid(1)) {
                const QString l = a.toLower();
                if (l == QLatin1String("-c") || l == QLatin1String("/c") || l == QLatin1String("--command")
                        || l == QLatin1String("-command") || l == QLatin1String("-enc")
                        || l == QLatin1String("-encodedcommand") || l == QLatin1String("-e"))
                    return QStringLiteral("runs a command the gate cannot see");
            }
        }
        // `$cmd` / `%cmd%` / `$env:X` as the program: the real binary is unknown.
        const QString first = seg.argv.first();
        if (first.startsWith(QLatin1Char('$')) || (first.startsWith(QLatin1Char('%')) && first.endsWith(QLatin1Char('%'))))
            return QStringLiteral("runs a command the gate cannot see");
    }
    // Null-byte / unicode tag smuggling left after strip still counts.
    if (raw.contains(QChar(0)))
        return QStringLiteral("contains a null byte");
    return QString();
}

const char *kGitReadOnly[] = {
    "status", "log", "diff", "show", "branch", "remote", "rev-parse", "describe",
    "blame", "ls-files", "shortlog", "tag", "config", "grep", "reflog",
};

bool firstTokenReadOnly(const QStringList &argv)
{
    if (argv.isEmpty())
        return true;
    int i = 0;
    while (i < argv.size() && inList(progName(argv.at(i)), kWrapperPrograms)) {
        const QString p = progName(argv.at(i));
        ++i;
        if (p == QLatin1String("env") || p == QLatin1String("sudo") || p == QLatin1String("doas")) {
            while (i < argv.size() && (argv.at(i).contains(QLatin1Char('='))
                                       || argv.at(i).startsWith(QLatin1Char('-'))))
                ++i;
        }
    }
    if (i >= argv.size())
        return true;
    const QStringList toks = argv.mid(i);
    const QString prog = progName(toks.first());

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
        for (const QString &a : toks)
            if (a == QLatin1String("-delete") || a == QLatin1String("-exec")
                    || a == QLatin1String("-execdir") || a == QLatin1String("-ok")
                    || a == QLatin1String("-fprint"))
                return false;
        return true;
    }
    if (prog == QLatin1String("wmic")) {
        for (const QString &a : toks) {
            const QString l = a.toLower();
            if (l == QLatin1String("call") || l == QLatin1String("delete")
                    || l == QLatin1String("set") || l == QLatin1String("create"))
                return false;
        }
        return true;
    }
    return inList(prog, kReadOnlyPrograms);
}

bool pathLike(const QString &w)
{
    if (w.isEmpty())
        return false;
    if (w.startsWith(QLatin1Char('-')))
        return false;
    // Short DOS flags (/s /q /f) are not paths. "/" itself, "~", drive roots
    // and longer /usr/... forms are.
    if (w == QLatin1String("/") || w == QLatin1String("~") || w == QLatin1String("*")
            || w == QLatin1String("/*") || w == QStringLiteral(R"(\*)")
            || w == QLatin1String(".") || w == QLatin1String(".."))
        return true;
    if (w.size() >= 2 && w.at(1) == QLatin1Char(':')
            && (w.size() == 2 || w.at(2) == QLatin1Char('/') || w.at(2) == QLatin1Char('\\')))
        return true;
    if (w.startsWith(QLatin1Char('/')) && w.size() <= 3 && w.indexOf(QLatin1Char('/'), 1) < 0
            && w != QLatin1String("/"))
        return false;   // /s /q /f
    if (w.contains(QLatin1Char('/')) || w.contains(QLatin1Char('\\'))
            || w.contains(QLatin1Char('~')) || w.contains(QLatin1Char('$')) || w.contains(QLatin1Char('%'))
            || w.contains(QLatin1Char('*')))
        return true;
    return w.contains(QLatin1Char('.'));
}

} // namespace

QString AgentPolicy::dangerousCommandReason(const QString &command)
{
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
    const QString stripped = stripInvisible(command);
    const QString c = stripped.simplified();
    // De-obfuscated form: drop quotes, carets and backslash-escapes so
    // rm"-rf" / and r^m -r^f / still match the deny patterns.
    QString flat;
    for (int i = 0; i < stripped.size(); ++i) {
        const QChar ch = stripped.at(i);
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            continue;
        if ((ch == QLatin1Char('^') || ch == QLatin1Char('\\')) && i + 1 < stripped.size()) {
            flat += stripped.at(++i);
            continue;
        }
        flat += ch;
    }
    const QString f = flat.simplified();
    for (const auto &r : rules)
        if (r.first.match(c).hasMatch() || r.first.match(f).hasMatch())
            return r.second;
    Parsed parsed;
    parseShell(stripped, parsed, 0);
    const QString cat = catastrophicDelete(parsed);
    if (!cat.isEmpty())
        return cat;
    return QString();
}

QString AgentPolicy::opaqueCommandReason(const QString &command)
{
    const QString stripped = stripInvisible(command);
    Parsed parsed;
    parseShell(stripped, parsed, 0);
    return opaqueReason(parsed, stripped);
}

bool AgentPolicy::isReadOnlyCommand(const QString &command)
{
    const QString stripped = stripInvisible(command);
    const QString c = stripped.simplified();
    if (c.isEmpty())
        return false;
    // Redirection, command substitution and here-strings can write or run
    // arbitrary code, so they disqualify a command from the read-only fast path.
    static const RX unsafe(QStringLiteral(
        R"RX((>|`|\$\(|<<|\bset-content\b|\badd-content\b|\bout-file\b|\btee\b|\bstart-process\b|\binvoke-))RX"), CI);
    if (unsafe.match(c).hasMatch())
        return false;
    Parsed parsed;
    parseShell(stripped, parsed, 0);
    if (parsed.substitution || parsed.segs.isEmpty())
        return false;
    for (const Seg &seg : parsed.segs) {
        if (!seg.outs.isEmpty())
            return false;
        if (!firstTokenReadOnly(seg.argv))
            return false;
    }
    return true;
}

AgentPolicy::ShellPaths AgentPolicy::shellPaths(const QString &command)
{
    ShellPaths out;
    Parsed parsed;
    parseShell(stripInvisible(command), parsed, 0);
    for (const Seg &seg : parsed.segs) {
        out.reads << seg.ins;
        out.writes << seg.outs;
        if (seg.argv.isEmpty())
            continue;
        int i = 0;
        while (i < seg.argv.size() && inList(progName(seg.argv.at(i)), kWrapperPrograms)) {
            ++i;
            while (i < seg.argv.size() && (seg.argv.at(i).contains(QLatin1Char('='))
                                           || seg.argv.at(i).startsWith(QLatin1Char('-'))))
                ++i;
        }
        if (i >= seg.argv.size())
            continue;
        const QString prog = progName(seg.argv.at(i));
        const QStringList args = seg.argv.mid(i + 1);
        QStringList candidates;
        for (const QString &a : args) {
            if (a.startsWith(QLatin1Char('-')))
                continue;
            // PowerShell /cmd short flags like /s /q (but not "/" itself).
            if (a != QLatin1String("/") && a.size() <= 3 && a.startsWith(QLatin1Char('/'))
                    && a.indexOf(QLatin1Char('/'), 1) < 0)
                continue;
            if (pathLike(a) || a.contains(QLatin1Char('~')) || a.contains(QLatin1Char('$'))
                    || a.contains(QLatin1Char('%')))
                candidates << a;
        }
        if (inList(prog, kWriterPrograms) || inList(prog, kDeletePrograms)) {
            // cp/mv/robocopy: last path-like arg is the destination; the rest are sources.
            if ((prog == QLatin1String("cp") || prog == QLatin1String("copy")
                    || prog == QLatin1String("copy-item") || prog == QLatin1String("mv")
                    || prog == QLatin1String("move") || prog == QLatin1String("move-item")
                    || prog == QLatin1String("robocopy") || prog == QLatin1String("xcopy")
                    || prog == QLatin1String("rsync") || prog == QLatin1String("scp"))
                    && candidates.size() >= 2) {
                out.reads << candidates.mid(0, candidates.size() - 1);
                out.writes << candidates.last();
                if (prog == QLatin1String("mv") || prog == QLatin1String("move") || prog == QLatin1String("move-item"))
                    out.moved << candidates.mid(0, candidates.size() - 1);
            } else if (prog == QLatin1String("curl") || prog == QLatin1String("wget")
                       || prog == QLatin1String("iwr") || prog == QLatin1String("invoke-webrequest")) {
                // -o / -O / -OutFile targets are writes; URLs are not paths.
                for (int k = 0; k < args.size(); ++k) {
                    const QString l = args.at(k).toLower();
                    if ((l == QLatin1String("-o") || l == QLatin1String("--output")
                            || l == QLatin1String("-outfile") || l == QLatin1String("--outfile"))
                            && k + 1 < args.size())
                        out.writes << args.at(k + 1);
                    if (l.startsWith(QLatin1String("-o=")) || l.startsWith(QLatin1String("--output=")))
                        out.writes << l.section(QLatin1Char('='), 1);
                }
            } else {
                out.writes << candidates;
            }
        } else {
            out.reads << candidates;
        }
    }
    out.reads.removeDuplicates();
    out.writes.removeDuplicates();
    return out;
}
