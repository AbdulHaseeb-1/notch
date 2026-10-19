#include "AgentPolicyInternal.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

namespace noch::agentpolicy {

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

} // namespace noch::agentpolicy

using namespace noch::agentpolicy;

namespace {

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

bool AgentPolicy::isInstructionPath(const QString &path)
{
    QString p = path.toLower();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));   // a Windows path, whatever the host
    const QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    const QString leaf = parts.last();
    if (leaf == QLatin1String("noch.md") || leaf == QLatin1String("agents.md") || leaf == QLatin1String("claude.md"))
        return true;
    return parts.contains(QStringLiteral(".noch"));
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
