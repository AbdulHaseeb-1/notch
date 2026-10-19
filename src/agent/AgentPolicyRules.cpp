#include "AgentPolicyInternal.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

using namespace noch::agentpolicy;

QString AgentPolicy::normalizeCommand(const QString &command)
{
    constexpr int kMaxRuleCommandChars = 400;
    const QString stripped = stripInvisible(command);
    if (stripped.size() > kMaxRuleCommandChars * 2)
        return QString();
    // Anything that chains, pipes, redirects, substitutes or expands: a rule
    // for `git status` must never also cover `git status; rm -rf x`.
    static const QString meta = QStringLiteral(";&|<>`$(){}\n\r^%");
    for (const QChar ch : stripped)
        if (meta.contains(ch))
            return QString();
    int dq = 0, sq = 0;
    for (const QChar ch : stripped) {
        dq += ch == QLatin1Char('"');
        sq += ch == QLatin1Char('\'');
    }
    if ((dq & 1) || (sq & 1))
        return QString();
    const QString out = stripped.simplified();
    return out.isEmpty() || out.size() > kMaxRuleCommandChars ? QString() : out;
}

namespace {

// Commands whose first words name a routine, bounded job: a prefix rule for
// one of these also covers its arguments. Nothing that fetches and runs
// arbitrary code (pip install, npx, curl ...), discards work (git reset,
// checkout, clean), or publishes (git push) is on the list.
const char *kRulePrefixes[] = {
    "git status", "git diff", "git log", "git show", "git add", "git commit", "git stash", "git branch", "git fetch",
    "npm test", "npm run build", "npm run lint", "npm run test", "npm ci",
    "pnpm test", "pnpm build", "pnpm lint", "yarn test", "yarn build", "yarn lint",
    "cargo build", "cargo test", "cargo check", "cargo fmt", "cargo clippy",
    "dotnet build", "dotnet test", "go build", "go test", "go vet", "go fmt",
    "cmake --build", "ctest", "ninja", "make", "msbuild", "pytest", "python -m pytest",
};

QString ruleKeyDir(const QString &raw, const QString &base)
{
    const QString lex = lexical(raw, base);
    return lex.isEmpty() ? QString() : lex.toLower();
}

// Folders too broad to hold a standing permission: a drive or filesystem
// root, the user's home, or anything above it.
bool dirTooBroadForRule(const QString &lexDir)
{
    const QString home = lexical(QDir::homePath(), QString()).toLower();
    const QString d = lexDir.toLower();
    if (d.isEmpty() || under(home, d))
        return true;   // the home folder itself or a parent of it
    const QStringList parts = d.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int depth = 0;
    for (const QString &p : parts)
        if (!(p.size() == 2 && p.endsWith(QLatin1Char(':'))))
            ++depth;
    return depth < 2;
}

} // namespace

AgentPolicy::SavedRule AgentPolicy::suggestRule(const QString &tool, const QJsonObject &input) const
{
    SavedRule r;
    if (tool == QLatin1String("bash")) {
        const QString cmd = normalizeCommand(input.value(QStringLiteral("command")).toString());
        if (cmd.isEmpty())
            return r;
        const QString workDir = input.value(QStringLiteral("working_dir")).toString().trimmed();
        const QString scope = lexical(workDir.isEmpty() ? m_base : workDir, m_base);
        if (scope.isEmpty() || dirTooBroadForRule(scope))
            return r;
        r.tool = tool;
        r.scope = scope;
        r.kind = QStringLiteral("exact");
        r.pattern = cmd;
        for (const char *p : kRulePrefixes) {
            const QString prefix = QString::fromLatin1(p);
            if (cmd == prefix || cmd.startsWith(prefix + QLatin1Char(' '))) {
                r.kind = QStringLiteral("prefix");
                r.pattern = prefix;
                break;
            }
        }
        r.label = (r.kind == QLatin1String("prefix") ? r.pattern + QStringLiteral(" …") : r.pattern)
                + QStringLiteral("  ·  in ") + scope;
        return r;
    }
    if (tool == QLatin1String("str_replace_editor")) {
        const QString path = input.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty() || isSecretsFile(path) || !credentialPathReason(path, m_base).isEmpty()
                || !protectedPathReason(path, m_base).isEmpty() || isPrivatePath(path))
            return r;
        const QString lex = lexical(path, m_base);
        const QString dir = QFileInfo(lex).path();
        if (dir.isEmpty() || dirTooBroadForRule(dir))
            return r;
        r.tool = tool;
        r.kind = QStringLiteral("dir");
        r.pattern = dir;
        r.label = QStringLiteral("Edit files in ") + dir;
        return r;
    }
    return r;
}

bool AgentPolicy::matchesSavedRule(const QString &tool, const QJsonObject &input) const
{
    for (const SavedRule &r : m_savedRules) {
        if (r.tool != tool || !r.valid())
            continue;
        if (tool == QLatin1String("bash")) {
            const QString cmd = normalizeCommand(input.value(QStringLiteral("command")).toString());
            if (cmd.isEmpty())
                continue;
            const QString workDir = input.value(QStringLiteral("working_dir")).toString().trimmed();
            const QString scope = lexical(workDir.isEmpty() ? m_base : workDir, m_base);
            if (scope.compare(r.scope, Qt::CaseInsensitive) != 0)
                continue;
            if (r.kind == QLatin1String("exact") && cmd == r.pattern)
                return true;
            if (r.kind == QLatin1String("prefix") && (cmd == r.pattern || cmd.startsWith(r.pattern + QLatin1Char(' '))))
                return true;
        } else if (tool == QLatin1String("str_replace_editor") && r.kind == QLatin1String("dir")) {
            const QString path = input.value(QStringLiteral("path")).toString().trimmed();
            if (path.isEmpty() || isSecretsFile(path))
                continue;
            const QString dirKey = ruleKeyDir(r.pattern, QString());
            if (dirKey.isEmpty())
                continue;
            // Every form of the path (as written, and with symlinks followed)
            // must be under the folder, or a link inside it could lead out.
            const QStringList keys = pathKeys(path, m_base);
            if (keys.isEmpty())
                continue;
            bool all = true;
            for (const QString &k : keys)
                all = all && under(k, dirKey) && k != dirKey;
            if (all)
                return true;
        }
    }
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
