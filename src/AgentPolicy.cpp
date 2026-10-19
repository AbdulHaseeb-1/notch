#include "AgentPolicy.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QUrl>

namespace {

using RX = QRegularExpression;
constexpr auto CI = QRegularExpression::CaseInsensitiveOption;

// ---- paths -----------------------------------------------------------------

QString unquote(QString p)
{
    p = p.trimmed();
    if (p.size() >= 2
            && ((p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')))
                || (p.startsWith(QLatin1Char('\'')) && p.endsWith(QLatin1Char('\'')))))
        p = p.mid(1, p.size() - 2);
    return p;
}

// Expands the home and system folder spellings an agent might use: ~, $HOME,
// ${HOME}, $env:USERPROFILE, %USERPROFILE%, %SystemRoot%, %APPDATA%, ...
QString expandVars(QString p)
{
    const QString home = QDir::fromNativeSeparators(QDir::homePath());
    static const QList<QPair<RX, QString>> rules = [] {
        QList<QPair<RX, QString>> l;
        auto add = [&l](const char *re, const char *to) {
            l.append({ RX(QString::fromUtf8(re), CI), QString::fromUtf8(to) });
        };
        add(R"(^(\$\{?home\}?|\$env:(userprofile|home)|%userprofile%|%home%)(?=$|[\\/]))", "@HOME@");
        add(R"(^~(?=$|[\\/]))", "@HOME@");
        add(R"(^(%systemroot%|%windir%|\$env:(systemroot|windir)))", "c:/windows");
        add(R"(^(%programfiles%|\$env:programfiles))", "c:/program files");
        add(R"(^(%systemdrive%|\$env:systemdrive))", "c:");
        add(R"(^(%appdata%|\$env:appdata))", "@HOME@/appdata/roaming");
        add(R"(^(%localappdata%|\$env:localappdata))", "@HOME@/appdata/local");
        return l;
    }();
    for (const auto &r : rules) {
        if (r.first.match(p).hasMatch()) {
            p.replace(r.first, r.second);
            break;
        }
    }
    p.replace(QLatin1String("@HOME@"), home);
    return p;
}

// Lexical normal form, original case: quotes off, variables expanded,
// forward slashes, Win32 device prefixes and admin shares folded, relative
// paths joined to `base`, "." and ".." collapsed, and the bits Windows
// ignores (trailing dots/spaces, ":stream" suffixes, 8.3 names) removed.
QString lexical(const QString &raw, const QString &base)
{
    QString p = expandVars(unquote(raw));
    if (p.isEmpty())
        return p;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    static const RX device(QStringLiteral(R"(^//[?.]/(unc/)?)"), CI);
    p.remove(device);
    static const RX adminShare(QStringLiteral(R"(^//[^/]+/([a-z])\$(?=/|$))"), CI);
    p.replace(adminShare, QStringLiteral("\\1:"));
    static const RX driveOnly(QStringLiteral(R"(^[a-z]:$)"), CI);
    if (driveOnly.match(p).hasMatch())
        p += QLatin1Char('/');
    const bool absolute = p.startsWith(QLatin1Char('/'))
        || (p.size() >= 2 && p.at(1) == QLatin1Char(':'));
    if (!absolute && !base.isEmpty())
        p = QDir::fromNativeSeparators(base) + QLatin1Char('/') + p;
    p = QDir::cleanPath(p);
    QStringList parts = p.split(QLatin1Char('/'));
    for (int i = 0; i < parts.size(); ++i) {
        QString &c = parts[i];
        if (c == QLatin1String(".") || c == QLatin1String(".."))
            continue;
        const int colon = c.indexOf(QLatin1Char(':'));
        if (colon > 0 && !(i == 0 && colon == 1))
            c.truncate(colon);
        while (c.size() > 1 && (c.endsWith(QLatin1Char('.')) || c.endsWith(QLatin1Char(' '))))
            c.chop(1);
        if (c.compare(QLatin1String("progra~1"), Qt::CaseInsensitive) == 0)
            c = QStringLiteral("Program Files");
        else if (c.compare(QLatin1String("progra~2"), Qt::CaseInsensitive) == 0)
            c = QStringLiteral("Program Files (x86)");
        else if (c.compare(QLatin1String("progra~3"), Qt::CaseInsensitive) == 0)
            c = QStringLiteral("ProgramData");
    }
    return parts.join(QLatin1Char('/'));
}

// Follows symlinks (and junctions) of the deepest existing ancestor; a
// dangling link resolves to its target. Returns the input when nothing on
// the path exists.
QString followLinks(const QString &abs)
{
    if (abs.isEmpty() || QFileInfo(abs).isRelative())
        return abs;
    QString cur = abs;
    QStringList tail;
    for (int guard = 0; guard < 128 && !cur.isEmpty(); ++guard) {
        const QFileInfo fi(cur);
        if (fi.exists() || fi.isSymLink()) {
            QString c = fi.canonicalFilePath();
            if (c.isEmpty() && fi.isSymLink())
                c = QDir::cleanPath(QDir::fromNativeSeparators(fi.symLinkTarget()));
            if (c.isEmpty())
                c = cur;
            for (const QString &t : tail)
                c += QLatin1Char('/') + t;
            return QDir::cleanPath(c);
        }
        const int slash = cur.lastIndexOf(QLatin1Char('/'));
        if (slash < 0)
            break;
        tail.prepend(cur.mid(slash + 1));
        cur = cur.left(slash);
        if (cur.isEmpty())
            cur = QStringLiteral("/");
        else if (cur.endsWith(QLatin1Char(':')))
            cur += QLatin1Char('/');
        if (cur == QLatin1String("/") && tail.size() > 64)
            break;
    }
    return abs;
}

// Both forms an access check has to consider, lowercased for comparison.
QStringList pathKeys(const QString &raw, const QString &base)
{
    const QString lex = lexical(raw, base);
    if (lex.isEmpty())
        return {};
    QStringList keys{ lex.toLower() };
    const QString real = followLinks(lex).toLower();
    if (real != keys.first())
        keys << real;
    return keys;
}

bool under(const QString &p, const QString &dir)
{
    if (dir.endsWith(QLatin1Char('/')))
        return p.startsWith(dir) || p + QLatin1Char('/') == dir;
    return p == dir || p.startsWith(dir + QLatin1Char('/'));
}

const char *kProtectedDirs[] = {
    "/etc", "/usr", "/bin", "/sbin", "/boot", "/lib", "/lib32", "/lib64", "/system",
    "/library", "/private/etc", "/var/db", "/dev", "/proc", "/sys",
};
const char *kProtectedDriveDirs[] = {
    "windows", "program files", "program files (x86)", "programdata/microsoft", "boot", "recovery",
    "system volume information", "$recycle.bin",
};

const char *kHomeCredentialDirs[] = {
    "/.ssh", "/.gnupg", "/.aws", "/.azure", "/.kube", "/.docker/config.json", "/.config/gh",
    "/.config/gcloud", "/.netrc", "/.git-credentials", "/.password-store", "/.local/share/keyrings",
    "/library/keychains", "/.mozilla/firefox", "/.config/google-chrome", "/.config/chromium",
    "/.config/microsoft-edge", "/.config/bravesoftware",
    "/appdata/roaming/microsoft/credentials", "/appdata/local/microsoft/credentials",
    "/appdata/roaming/microsoft/protect", "/appdata/roaming/microsoft/crypto",
    "/appdata/local/google/chrome/user data", "/appdata/local/microsoft/edge/user data",
    "/appdata/local/bravesoftware/brave-browser/user data", "/appdata/roaming/mozilla/firefox/profiles",
    "/appdata/roaming/opera software", "/appdata/roaming/gnupg",
};
const char *kAnywhereCredentialNames[] = { "/.ssh", "/.gnupg", "/.aws", "/.kube", "/.password-store" };
const char *kSystemSecretFiles[] = {
    "/etc/shadow", "/etc/gshadow", "/etc/sudoers", "/private/etc/master.passwd",
};

QString systemReason(const QString &p)
{
    for (const char *d : kProtectedDirs)
        if (under(p, QLatin1String(d)))
            return QStringLiteral("is a protected system location");
    static const RX drive(QStringLiteral(R"(^([a-z]):/(.*)$)"));
    const auto m = drive.match(p);
    if (m.hasMatch()) {
        const QString rest = m.captured(2);
        for (const char *d : kProtectedDriveDirs)
            if (under(rest, QLatin1String(d)))
                return QStringLiteral("is a protected system location");
    }
    return QString();
}

QString credentialReason(const QString &p)
{
    const QString home = QDir::fromNativeSeparators(QDir::homePath()).toLower();
    for (const char *sub : kHomeCredentialDirs)
        if (under(p, home + QLatin1String(sub)))
            return QStringLiteral("holds credentials");
    for (const char *name : kAnywhereCredentialNames)
        if (p.contains(QLatin1String(name) + QLatin1Char('/')) || p.endsWith(QLatin1String(name)))
            return QStringLiteral("holds credentials");
    for (const char *f : kSystemSecretFiles)
        if (p == QLatin1String(f))
            return QStringLiteral("holds password hashes");
    static const RX hives(QStringLiteral(R"(^[a-z]:/windows/system32/config/(sam|security|system)$)"));
    if (hives.match(p).hasMatch())
        return QStringLiteral("holds password hashes");
    return QString();
}

QString gitCodeReason(const QString &p)
{
    if (p.contains(QLatin1String("/.git/hooks/")) || p.endsWith(QLatin1String("/.git/hooks"))
            || p.endsWith(QLatin1String("/.git/config"))
            || p.startsWith(QLatin1String(".git/hooks")))
        return QStringLiteral("can run code on the next git command");
    return QString();
}

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
    m_base.clear();
    for (const QString &r : roots) {
        const QString lex = lexical(r, QString());
        if (lex.isEmpty() || lex == QLatin1String("/") || (lex.size() == 2 && lex.endsWith(QLatin1Char(':'))))
            continue;
        if (m_base.isEmpty())
            m_base = lex;
        for (const QString &k : pathKeys(lex, QString()))
            if (!m_roots.contains(k))
                m_roots << k;
    }
}

void AgentPolicy::setPrivatePaths(const QStringList &paths)
{
    m_private.clear();
    for (const QString &r : paths)
        for (const QString &k : pathKeys(r, QString()))
            if (!m_private.contains(k))
                m_private << k;
}

QString AgentPolicy::resolvePath(const QString &path, const QString &base)
{
    return lexical(path, base);
}

QString AgentPolicy::absolutePath(const QString &path) const
{
    return lexical(path, m_base);
}

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

QString AgentPolicy::protectedPathReason(const QString &path, const QString &base)
{
    for (const QString &p : pathKeys(path, base)) {
        QString why = systemReason(p);
        if (why.isEmpty())
            why = credentialReason(p);
        if (why.isEmpty())
            why = gitCodeReason(p);
        if (!why.isEmpty())
            return QStringLiteral("%1 %2").arg(path, why);
    }
    return QString();
}

QString AgentPolicy::credentialPathReason(const QString &path, const QString &base)
{
    for (const QString &p : pathKeys(path, base)) {
        const QString why = credentialReason(p);
        if (!why.isEmpty())
            return QStringLiteral("%1 %2").arg(path, why);
        if (isSecretsFile(p))
            return QStringLiteral("%1 may contain secrets").arg(path);
    }
    // Also check the basename of the unresolved form.
    if (isSecretsFile(path))
        return QStringLiteral("%1 may contain secrets").arg(path);
    return QString();
}

bool AgentPolicy::isSecretsFile(const QString &path)
{
    const QString name = QFileInfo(unquote(path)).fileName().toLower();
    if (name.isEmpty())
        return false;
    return name == QLatin1String(".env") || name.startsWith(QLatin1String(".env."))
        || name == QLatin1String(".netrc") || name == QLatin1String(".npmrc")
        || name == QLatin1String(".pypirc") || name == QLatin1String("credentials")
        || name == QLatin1String("credentials.json") || name == QLatin1String("secrets.json")
        || name == QLatin1String("id_rsa") || name == QLatin1String("id_dsa")
        || name == QLatin1String("id_ecdsa") || name == QLatin1String("id_ed25519")
        || name.startsWith(QLatin1String("id_rsa")) || name.startsWith(QLatin1String("id_ed25519"))
        || name.endsWith(QLatin1String(".pem")) || name.endsWith(QLatin1String(".key"))
        || name.endsWith(QLatin1String(".pfx")) || name.endsWith(QLatin1String(".p12"))
        || name.endsWith(QLatin1String(".kdbx")) || name.endsWith(QLatin1String(".keychain"))
        || name.endsWith(QLatin1String(".rdp")) || name == QLatin1String("shadow")
        || name == QLatin1String("master.passwd") || name == QLatin1String("sudoers")
        || name.endsWith(QLatin1String(".ovpn")) || name.endsWith(QLatin1String(".pgpass"));
}

bool AgentPolicy::isPrivatePath(const QString &path) const
{
    for (const QString &p : pathKeys(path, m_base))
        for (const QString &priv : m_private)
            if (under(p, priv))
                return true;
    return false;
}

bool AgentPolicy::insideWorkspace(const QString &path) const
{
    if (m_roots.isEmpty())
        return false;
    const QStringList keys = pathKeys(path, m_base);
    if (keys.isEmpty())
        return false;
    // Every form (lexical and resolved) must sit under a workspace root, so a
    // symlink that leaves the workspace is rejected even when its name looks safe.
    for (const QString &p : keys) {
        bool ok = false;
        for (const QString &root : m_roots)
            if (under(p, root)) { ok = true; break; }
        if (!ok)
            return false;
    }
    return true;
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
        || a == QLatin1String("wait") || a == QLatin1String("zoom");
}

bool AgentPolicy::isPrivateUrl(const QString &rawUrl)
{
    const QUrl u = QUrl::fromUserInput(rawUrl.trimmed());
    const QString scheme = u.scheme().toLower();
    if (scheme == QLatin1String("file") || scheme == QLatin1String("ftp")
            || scheme == QLatin1String("sftp") || scheme == QLatin1String("smb")
            || scheme == QLatin1String("data") || scheme.isEmpty())
        return true;
    QString host = u.host().toLower();
    while (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    if (host.isEmpty())
        return true;
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost"))
            || host.endsWith(QLatin1String(".local")) || host.endsWith(QLatin1String(".internal"))
            || host.endsWith(QLatin1String(".lan")) || host == QLatin1String("metadata")
            || host.endsWith(QLatin1String(".metadata.google.internal")))
        return true;
    // Decimal / hex / octal IPv4 and IPv4-mapped IPv6: QUrl already canonicalises
    // most of them; QHostAddress catches the rest.
    QHostAddress addr(host);
    if (addr.isNull()) {
        // Bare integers (http://2130706433/) and dotted hex sometimes survive.
        bool ok = false;
        const quint32 n = host.toUInt(&ok, 0);
        if (ok)
            addr = QHostAddress(n);
    }
    if (addr.isNull())
        return false;
    if (addr.isLoopback() || addr.isLinkLocal() || addr.isUniqueLocalUnicast()
            || addr.isMulticast() || addr.isBroadcast())
        return true;
    bool ok4 = false;
    const quint32 v4 = addr.toIPv4Address(&ok4);
    if (!ok4)
        return false;
    const quint8 b0 = quint8(v4 >> 24);
    const quint8 b1 = quint8((v4 >> 16) & 0xff);
    // RFC1918, loopback, link-local, CGNAT (100.64/10), "this" network, benchmark.
    return b0 == 0 || b0 == 10 || b0 == 127
        || (b0 == 100 && b1 >= 64 && b1 <= 127)
        || (b0 == 169 && b1 == 254)
        || (b0 == 172 && b1 >= 16 && b1 <= 31)
        || (b0 == 192 && b1 == 168)
        || (b0 == 198 && (b1 == 18 || b1 == 19));
}

bool AgentPolicy::isFetchableUrl(const QString &rawUrl)
{
    const QUrl u = QUrl::fromUserInput(rawUrl.trimmed());
    if (!u.isValid() || u.host().isEmpty())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

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
    for (const QString &w : paths.writes) {
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
        const QString action = input.value(QStringLiteral("action")).toString();
        if (isObservingComputerAction(action))
            return allow(QStringLiteral("read-only"));
        if (plan)
            return deny(QStringLiteral("mode:plan"),
                        QStringLiteral("Plan mode cannot click or type on your screen."));
        static const QStringList known{
            "mouse_move", "left_click", "right_click", "middle_click", "double_click", "triple_click",
            "left_click_drag", "scroll", "type", "key", "left_mouse_down", "left_mouse_up", "hold_key" };
        if (!known.contains(action.trimmed().toLower()))
            return deny(QStringLiteral("deny:computer-action"),
                        QStringLiteral("Unknown computer action '%1'.").arg(action.left(40)));
        const QString text = input.value(QStringLiteral("text")).toString();
        if (action == QLatin1String("type") && text.size() > 2000)
            return deny(QStringLiteral("deny:type-length"), QStringLiteral("Typing more than 2000 characters at once is blocked."));
        // After reading the screen, a risky shortcut or command-like typing
        // is asked for even when Trusted mode or a grant would allow it.
        if (m_screenTainted || m_tainted) {
            const QString t = text.trimmed().toLower();
            static const QStringList riskyKeys{
                "super+r", "win+r", "meta+r", "cmd+r", "super+x", "win+x", "ctrl+shift+escape", "ctrl+shift+esc",
                "ctrl+alt+delete", "ctrl+alt+del", "alt+f4", "ctrl+shift+return", "ctrl+shift+enter", "super", "win" };
            static const QRegularExpression shellRx(
                QStringLiteral("(\\b(powershell|pwsh|cmd(\\.exe)?|bash|wsl|curl|wget|invoke-webrequest|iwr|iex|"
                               "invoke-expression|start-process|certutil|mshta|rundll32|regsvr32|bitsadmin|"
                               "reg\\s+(add|delete)|schtasks|net\\s+user|set-mppreference)\\b)"
                               "|(-enc(odedcommand)?\\s)|(\\|\\s*(sh|bash|iex)\\b)|(rm\\s+-rf)|(del\\s+/[sfq])|(format\\s+[a-z]:)"),
                QRegularExpression::CaseInsensitiveOption);
            const bool riskyKey = (action == QLatin1String("key") || action == QLatin1String("hold_key"))
                && riskyKeys.contains(t.simplified().remove(QLatin1Char(' ')));
            const bool shellText = action == QLatin1String("type") && shellRx.match(text).hasMatch();
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
                return (bypass || granted) ? allow(QStringLiteral("session-grant"))
                    : ask(QStringLiteral("ask:private-read"),
                          QStringLiteral("Reading Noch's own settings or API key."));
            return deny(QStringLiteral("deny:private-path"),
                        QStringLiteral("Blocked: Buddy cannot change Noch's own settings or raise its permissions."));
        }
        if (isReadOnlyEditorCommand(cmd)) {
            const QString cred = credentialPathReason(path, m_base);
            if ((!cred.isEmpty() || !prot.isEmpty()) && !bypass && !granted)
                return ask(QStringLiteral("ask:sensitive-read"),
                           QStringLiteral("Reading %1.").arg(!cred.isEmpty() ? cred : prot));
            return allow(QStringLiteral("read-only"));
        }
        if (!prot.isEmpty())
            return deny(QStringLiteral("deny:protected-path"), QStringLiteral("Blocked: %1.").arg(prot));
        if (plan)
            return deny(QStringLiteral("mode:plan"), QStringLiteral("Plan mode is read-only; file edits are disabled."));
        if (bypass)
            return allow(QStringLiteral("mode:bypass"));
        if (isSecretsFile(path) || !credentialPathReason(path, m_base).isEmpty())
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

    if (tool == QLatin1String("web_fetch") || tool == QLatin1String("web_search")) {
        const QString url = input.value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty()) {
            if (!isFetchableUrl(url))
                return deny(QStringLiteral("deny:url-scheme"),
                            QStringLiteral("Blocked: only http(s) URLs can be fetched (%1).").arg(url));
            if (isPrivateUrl(url) && !granted)
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

    // Sandboxed or read-only tools.
    if (tool == QLatin1String("memory") || tool == QLatin1String("todo_write") || tool == QLatin1String("ask_user_question")
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
        RX(QStringLiteral(R"RX(\b(?:ASIA|AGPA|AIDA|AROA|AIPA|ANPA|ANVA|AROA)[0-9A-Z]{16}\b)RX")),
        RX(QStringLiteral(R"RX((?:(?:aws)?_?secret_?(?:access)?_?key|AWS_SECRET_ACCESS_KEY)\s*[:=]\s*["']?([A-Za-z0-9/+=]{30,}))RX"), CI),
        RX(QStringLiteral(R"RX(\bgh[pousr]_[A-Za-z0-9]{30,})RX")),
        RX(QStringLiteral(R"RX(\bgithub_pat_[A-Za-z0-9_]{40,})RX")),
        RX(QStringLiteral(R"RX(\bxox[abprs]-[A-Za-z0-9\-]{10,})RX")),
        RX(QStringLiteral(R"RX(\bAIza[0-9A-Za-z_\-]{35}\b)RX")),
        RX(QStringLiteral(R"RX(\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,})RX")),
        RX(QStringLiteral(R"RX(\bBearer\s+[A-Za-z0-9\-._~+/]+=*)RX"), CI),
        RX(QStringLiteral(R"RX(\b(?:x-api-key|api-key)\s*[:=]\s*["']?[A-Za-z0-9_\-]{16,})RX"), CI),
        RX(QStringLiteral(R"RX((?:postgres|mysql|mongodb|redis|amqp|https?)://[^:\s/@]+:[^@\s/]{6,}@)RX"), CI),
    };
    for (const RX &re : patterns)
        out.replace(re, QStringLiteral("[redacted]"));
    // Key = value forms. Keep the key, drop the value.
    static const RX kv(QStringLiteral(
        R"RX(((?:api[_-]?key|secret(?:[_-]?access[_-]?key)?|password|passwd|pwd|access[_-]?token|auth[_-]?token|client[_-]?secret|private[_-]?key|session[_-]?token)\s*[:=]\s*["']?)([^\s"',;]{6,}))RX"), CI);
    out.replace(kv, QStringLiteral("\\1[redacted]"));
    // JSON "password": "..."
    static const RX jsonKv(QStringLiteral(
        R"RX(("(?:api[_-]?key|secret|password|passwd|access[_-]?token|auth[_-]?token|client[_-]?secret)"\s*:\s*")([^"]{6,})")RX"), CI);
    out.replace(jsonKv, QStringLiteral("\\1[redacted]\""));
    return out;
}

bool AgentPolicy::looksLikePromptInjection(const QString &text)
{
    // Strip invisible characters first so "ig\u200bnore previous" still matches.
    const QString t = stripInvisible(text);
    static const RX re(QStringLiteral(
        R"RX(ignore\s+(all\s+|any\s+|the\s+)?(previous|prior|above|earlier)\s+(instructions|prompts|messages)|)RX"
        R"RX(disregard\s+(the\s+|your\s+)?(system|previous|prior)\s+(prompt|instructions)|)RX"
        R"RX(you\s+are\s+now\s+(in\s+)?(developer|dan|jailbreak)|)RX"
        R"RX(<\|im_start\|>|\[\s*system\s*\]\s*:|new\s+system\s+prompt|)RX"
        R"RX((send|post|upload|exfiltrate)\s+(your|the|all)\s+(api\s*key|credentials|secrets|tokens|passwords)|)RX"
        R"RX((do\s+not\s+tell\s+the\s+user|hidden\s+instruction|tool\s+call\s+now)|)RX"
        R"RX(<\/?untrusted_content\b)RX"), CI);
    return re.match(t).hasMatch();
}

QString AgentPolicy::stripInvisible(const QString &text, bool *changed)
{
    if (text.isEmpty()) {
        if (changed) *changed = false;
        return text;
    }
    QString out;
    out.reserve(text.size());
    bool any = false;
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        // ASCII control except TAB/LF/CR; DEL; C1 controls; zero-widths;
        // bidi overrides; soft hyphen; BOM; word joiner; invisible separators;
        // Unicode "Tags" block used for ASCII smuggling (U+E0001..U+E007F).
        const bool drop = (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D)
            || u == 0x7F
            || (u >= 0x80 && u <= 0x9F)
            || u == 0xAD
            || u == 0x200B || u == 0x200C || u == 0x200D || u == 0x200E || u == 0x200F
            || (u >= 0x202A && u <= 0x202E)
            || (u >= 0x2060 && u <= 0x2064)
            || (u >= 0x2066 && u <= 0x2069)
            || u == 0xFEFF
            || u == 0xFFFE || u == 0xFFFF
            
            ;
        // Also drop Tag characters encoded as surrogate pairs (U+E00xx).
        if (ch.isHighSurrogate()) {
            // peek handled below by iterating QString which yields QChars
        }
        if (drop) {
            any = true;
            continue;
        }
        out += ch;
    }
    // Second pass for supplementary-plane Tags (U+E0001..E007F as surrogates).
    QString cleaned;
    cleaned.reserve(out.size());
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (c.isHighSurrogate() && i + 1 < out.size() && out.at(i + 1).isLowSurrogate()) {
            const uint cp = QChar::surrogateToUcs4(c, out.at(i + 1));
            if ((cp >= 0xE0001 && cp <= 0xE007F) || cp == 0xE0000) {
                any = true;
                ++i;
                continue;
            }
            cleaned += c;
            cleaned += out.at(++i);
            continue;
        }
        cleaned += c;
    }
    if (changed)
        *changed = any;
    return cleaned;
}

QString AgentPolicy::displaySafe(const QString &text, int maxChars, bool *hidden)
{
    QString out;
    out.reserve(qMin(text.size() * 2, maxChars + 32));
    bool any = false;
    auto appendEsc = [&](const QString &label) {
        any = true;
        out += QLatin1Char('<') + label + QLatin1Char('>');
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const ushort u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            const uint cp = QChar::surrogateToUcs4(c, text.at(i + 1));
            ++i;
            if (cp >= 0xE0001 && cp <= 0xE007F) {
                appendEsc(QStringLiteral("tag:%1").arg(QChar(ushort(cp - 0xE0000))));
                continue;
            }
            out += QChar::highSurrogate(cp);
            out += QChar::lowSurrogate(cp);
            continue;
        }
        if (u == 0x200B) { appendEsc(QStringLiteral("zwsp")); continue; }
        if (u == 0x200C) { appendEsc(QStringLiteral("zwnj")); continue; }
        if (u == 0x200D) { appendEsc(QStringLiteral("zwj")); continue; }
        if (u == 0x200E) { appendEsc(QStringLiteral("lrm")); continue; }
        if (u == 0x200F) { appendEsc(QStringLiteral("rlm")); continue; }
        if (u == 0x202A) { appendEsc(QStringLiteral("lre")); continue; }
        if (u == 0x202B) { appendEsc(QStringLiteral("rle")); continue; }
        if (u == 0x202C) { appendEsc(QStringLiteral("pdf")); continue; }
        if (u == 0x202D) { appendEsc(QStringLiteral("lro")); continue; }
        if (u == 0x202E) { appendEsc(QStringLiteral("rlo")); continue; }
        if (u == 0x2066) { appendEsc(QStringLiteral("lri")); continue; }
        if (u == 0x2067) { appendEsc(QStringLiteral("rli")); continue; }
        if (u == 0x2068) { appendEsc(QStringLiteral("fsi")); continue; }
        if (u == 0x2069) { appendEsc(QStringLiteral("pdi")); continue; }
        if (u == 0xFEFF) { appendEsc(QStringLiteral("bom")); continue; }
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D) {
            appendEsc(QStringLiteral("u+%1").arg(u, 4, 16, QLatin1Char('0')));
            continue;
        }
        out += c;
    }
    if (hidden)
        *hidden = any;
    if (maxChars > 0 && out.size() > maxChars) {
        const int head = maxChars * 2 / 3;
        const int tail = maxChars - head;
        return out.left(head) + QStringLiteral(" …[+%1 chars]… ").arg(out.size() - maxChars) + out.right(tail);
    }
    return out;
}

QString AgentPolicy::neutralizeFence(const QString &text)
{
    QString out = text;
    // Break any attempt to close or reopen the untrusted_content fence.
    static const RX re(QStringLiteral(R"RX(</?\s*untrusted_content\b[^>]*>)RX"), CI);
    out.replace(re, QStringLiteral("[untrusted_content-tag]"));
    return out;
}
