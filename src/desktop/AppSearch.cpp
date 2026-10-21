#include "AppSearch.h"

#include "Calc.h"
#include "RunningApps.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace {

struct CommandDef {
    const char *id;
    const char *title;
    const char *glyph;
};

// The built-ins a search field should beat Windows' own menus to.
const CommandDef kCommands[] = {
    {"lock", QT_TRANSLATE_NOOP("AppSearch", "Lock the computer"), "lock"},
    {"sleep", QT_TRANSLATE_NOOP("AppSearch", "Sleep"), "zzz"},
    {"mic", QT_TRANSLATE_NOOP("AppSearch", "Mute or unmute the microphone"), "mic"},
    {"timer", QT_TRANSLATE_NOOP("AppSearch", "Focus timer"), "timer"},
    {"clipboard", QT_TRANSLATE_NOOP("AppSearch", "Clipboard history"), "clipboard"},
    {"taskmgr", QT_TRANSLATE_NOOP("AppSearch", "Task Manager"), "activity"},
    {"winsettings", QT_TRANSLATE_NOOP("AppSearch", "Windows settings"), "sliders"},
    {"noch", QT_TRANSLATE_NOOP("AppSearch", "Noch settings"), "settings"},
};

QString iconIdFor(const QString &path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
}

// -1 no match; higher is better. Expects haystack and needle to be lowercased.
int scoreText(const QString &haystack, const QString &needle)
{
    if (needle.isEmpty() || haystack.isEmpty())
        return -1;
    if (haystack == needle)
        return 1000;
    if (haystack.startsWith(needle))
        return 900 - qMin(90, haystack.size() - needle.size());

    for (int i = 1; i + needle.size() <= haystack.size(); ++i) {
        if (haystack.at(i - 1) != QLatin1Char(' ') && haystack.at(i - 1) != QLatin1Char('-')
            && haystack.at(i - 1) != QLatin1Char('_'))
            continue;
        if (haystack.mid(i).startsWith(needle))
            return 800 - qMin(90, haystack.size() - needle.size());
    }

    const qsizetype index = haystack.indexOf(needle);
    if (index > 0)
        return 700 - qMin(200, int(index) + int(haystack.size() - needle.size()));

    int j = 0;
    for (int i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (haystack.at(i) == needle.at(j))
            ++j;
    }
    if (j == needle.size())
        return 400 - qMin(200, int(haystack.size() - needle.size()));
    return -1;
}

// Links in the Start Menu that do not launch anything useful on their own.
bool isNoise(const QString &name)
{
    static const QRegularExpression noise(QStringLiteral(
        "uninstall|\\buninstaller|remove\\b|\\breadme|\\bhelp\\b|documentation|\\bmanual\\b|"
        "\\blicense|release notes|changelog|update\\b|\\brepair\\b|\\bmodify\\b|\\bwebsite\\b|"
        "\\bsupport\\b|\\babout\\b|\\bwebsite|deinstallieren|helpcenter"),
        QRegularExpression::CaseInsensitiveOption);
    return noise.match(name).hasMatch();
}

} // namespace

AppSearch::AppSearch(RunningApps *running, QObject *parent)
    : QObject(parent)
    , m_running(running)
{
}

AppSearch::~AppSearch()
{
    // Wait for a running scan instead of destroying it mid-run (same pattern
    // as MusicPlayer); the scan is bounded.
    if (m_scanThread)
        m_scanThread->wait(30000);
}

QString AppSearch::iconPathForId(const QString &iconId) const
{
    QMutexLocker lock(&m_iconMutex);
    return m_icons.value(iconId);
}

void AppSearch::ensureIndex()
{
    bool expected = false;
    if (!m_indexed.compare_exchange_strong(expected, true))
        return;

    const QStringList roots = {
        QDir::cleanPath(qEnvironmentVariable("APPDATA")
                        + QStringLiteral("/Microsoft/Windows/Start Menu/Programs")),
        QDir::cleanPath(qEnvironmentVariable("ProgramData")
                        + QStringLiteral("/Microsoft/Windows/Start Menu/Programs")),
    };

    // The first palette keystroke used to walk the whole Start Menu tree on
    // the GUI thread; a slow share or a large folder list froze typing. Collect
    // the entries on a worker thread and merge them back when it is done.
    QThread *thread = QThread::create([this, roots]() {
        QList<Entry> entries;
        QHash<QString, QString> icons;

        QSet<QString> seen;
        QSet<QString> seenExes;
        for (const QString &root : roots) {
            if (root.isEmpty() || !QDir(root).exists())
                continue;
            QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                const QFileInfo fi(file);
                const QString name = fi.completeBaseName().trimmed();
                if (name.size() < 2 || isNoise(name))
                    continue;

                QString exe;
                const QString target = fi.symLinkTarget();
                if (!target.isEmpty()) {
                    const QFileInfo tfi(target);
                    // exists() probes the target file; on a UNC path that can
                    // stall for seconds per shortcut, so trust the extension
                    // there (ShellExecute refuses dead targets at launch).
                    const bool unc = target.startsWith(QLatin1String("\\\\"))
                                     || target.startsWith(QLatin1String("//"));
                    if (!unc && !tfi.exists())
                        continue;
                    if (tfi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0)
                        exe = tfi.fileName().toLower();
                    else
                        continue; // skip non-executable shortcuts (URLs, docs, folders)
                }

                const QString key = name.toLower();
                if (seen.contains(key) || (!exe.isEmpty() && seenExes.contains(exe)))
                    continue;
                seen.insert(key);
                if (!exe.isEmpty())
                    seenExes.insert(exe);

                Entry entry;
                entry.name = name;
                entry.lowerName = name.toLower();
                entry.path = file;
                entry.exe = exe;
                entry.lowerExe = exe.toLower();
                entry.iconId = iconIdFor(file);
                entries.append(entry);
                icons.insert(entry.iconId, file);
            }
        }

        QMetaObject::invokeMethod(this,
                                  [this, entries = std::move(entries),
                                   icons = std::move(icons)]() mutable {
                                      applyIndex(std::move(entries), std::move(icons));
                                  },
                                  Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_scanThread = thread;
    thread->start();
}

void AppSearch::applyIndex(QList<Entry> entries, QHash<QString, QString> icons)
{
    m_scanThread = nullptr; // finished (deleteLater owns the object now)

    // File Explorer is always available even if not in Start Menu Programs.
#ifdef Q_OS_WIN
    bool haveExplorer = false;
    for (const Entry &entry : entries) {
        if (entry.exe.compare(QLatin1String("explorer.exe"), Qt::CaseInsensitive) == 0) {
            haveExplorer = true;
            break;
        }
    }
    const QString explorerPath = QDir::cleanPath(
        qEnvironmentVariable("WINDIR") + QStringLiteral("/explorer.exe"));
    if (!QFileInfo::exists(explorerPath))
        haveExplorer = true;
    if (!haveExplorer) {
        Entry entry;
        entry.name = QStringLiteral("File Explorer");
        entry.lowerName = QStringLiteral("file explorer");
        entry.path = explorerPath;
        entry.exe = QStringLiteral("explorer.exe");
        entry.lowerExe = QStringLiteral("explorer.exe");
        entry.iconId = iconIdFor(explorerPath);
        entries.append(entry);
        icons.insert(entry.iconId, explorerPath);
    }
#endif

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    {
        QMutexLocker lock(&m_iconMutex);
        for (auto it = icons.constBegin(); it != icons.constEnd(); ++it)
            m_icons.insert(it.key(), it.value());
    }
    m_entries = std::move(entries);
    emit indexReady();
}

QVariantList AppSearch::availableApps(const QString &query)
{
    ensureIndex();

    // Merge indexed apps with any currently running apps not already in the index.
    QList<Entry> combined = m_entries;
    if (m_running) {
        QSet<QString> knownExes;
        QSet<QString> knownNames;
        for (const Entry &e : std::as_const(m_entries)) {
            if (!e.exe.isEmpty())
                knownExes.insert(e.exe);
            knownNames.insert(e.name.toLower());
        }
        for (const QVariant &v : m_running->apps()) {
            const QVariantMap m = v.toMap();
            const QString exe = m.value(QStringLiteral("exe")).toString().toLower();
            const QString name = m.value(QStringLiteral("name")).toString().trimmed();
            const QString path = m.value(QStringLiteral("path")).toString();
            const QString iconId = m.value(QStringLiteral("iconId")).toString();
            if (name.isEmpty() || path.isEmpty())
                continue;
            if ((!exe.isEmpty() && knownExes.contains(exe)) || knownNames.contains(name.toLower()))
                continue;
            if (!exe.isEmpty())
                knownExes.insert(exe);
            knownNames.insert(name.toLower());

            Entry extra;
            extra.name = name;
            extra.lowerName = name.toLower();
            extra.path = path;
            extra.exe = exe;
            extra.lowerExe = exe;
            extra.iconId = iconId.isEmpty() ? iconIdFor(path) : iconId;
            combined.append(extra);
            {
                QMutexLocker lock(&m_iconMutex);
                m_icons.insert(extra.iconId, path);
            }
        }
    }

    const QString text = query.trimmed();
    const QString needle = text.toLower();
    struct ScoredEntry {
        int score;
        Entry entry;
    };
    QList<ScoredEntry> matches;
    matches.reserve(combined.size());

    for (const Entry &entry : std::as_const(combined)) {
        if (needle.isEmpty()) {
            matches.append({0, entry});
        } else {
            const int score = qMax(scoreText(entry.lowerName, needle),
                                   scoreText(entry.lowerExe, needle) - 50);
            if (score > 0)
                matches.append({score, entry});
        }
    }

    if (text.isEmpty()) {
        std::sort(matches.begin(), matches.end(),
                  [](const ScoredEntry &a, const ScoredEntry &b) {
                      return a.entry.name.compare(b.entry.name, Qt::CaseInsensitive) < 0;
                  });
    } else {
        std::stable_sort(matches.begin(), matches.end(),
                         [](const ScoredEntry &a, const ScoredEntry &b) {
                             if (a.score != b.score)
                                 return a.score > b.score;
                             return a.entry.name.compare(b.entry.name, Qt::CaseInsensitive) < 0;
                         });
    }

    QVariantList result;
    result.reserve(matches.size());
    for (const ScoredEntry &s : std::as_const(matches)) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), s.entry.name);
        row.insert(QStringLiteral("path"), s.entry.path);
        row.insert(QStringLiteral("exe"), s.entry.exe);
        row.insert(QStringLiteral("iconId"), s.entry.iconId);
        result.append(row);
    }
    return result;
}

QVariantList AppSearch::search(const QString &query)
{
    const QString text = query.trimmed();

    struct Hit {
        int score;
        QVariantMap row;
    };
    QList<Hit> hits;

    const auto addHit = [&hits](int score, QVariantMap row) {
        hits.append({score, std::move(row)});
    };

    const auto commandRow = [](const CommandDef &def) {
        QVariantMap row;
        row.insert(QStringLiteral("kind"), QStringLiteral("command"));
        row.insert(QStringLiteral("title"), QCoreApplication::translate("AppSearch", def.title));
        row.insert(QStringLiteral("subtitle"), QCoreApplication::translate("AppSearch", "Command"));
        row.insert(QStringLiteral("id"), QString::fromLatin1(def.id));
        row.insert(QStringLiteral("icon"), QString::fromLatin1(def.glyph));
        return row;
    };

    if (text.isEmpty()) {
        // Nothing typed: offer the commands, which is what people reach for
        // when they open a launcher without a plan.
        QVariantList results;
        for (const CommandDef &def : kCommands)
            results.append(commandRow(def));
        return results;
    }

    // Calculator: the most precise intent there is, so it takes the top row.
    QString expression = text;
    if (expression.endsWith(QLatin1Char('=')))
        expression.chop(1);
    if (nochcalc::looksLikeExpression(expression)) {
        const QString value = nochcalc::evaluate(expression);
        if (!value.isEmpty()) {
            QVariantMap row;
            row.insert(QStringLiteral("kind"), QStringLiteral("calc"));
            row.insert(QStringLiteral("title"), value);
            row.insert(QStringLiteral("subtitle"),
                       tr("Copy result — %1 = %2").arg(expression.trimmed(), value));
            row.insert(QStringLiteral("icon"), QStringLiteral("equals"));
            row.insert(QStringLiteral("value"), value);
            addHit(2000, row);
        }
    }

    ensureIndex();

    const QString needle = text.toLower();

    for (const Entry &entry : m_entries) {
        const int score = scoreText(entry.lowerName, needle);
        if (score > 0) {
            QVariantMap row;
            row.insert(QStringLiteral("kind"), QStringLiteral("app"));
            row.insert(QStringLiteral("title"), entry.name);
            row.insert(QStringLiteral("subtitle"), tr("Application"));
            row.insert(QStringLiteral("path"), entry.path);
            row.insert(QStringLiteral("iconId"), entry.iconId);
            addHit(score + 20, row);
        }
    }

    for (const CommandDef &def : kCommands) {
        const QString title = QCoreApplication::translate("AppSearch", def.title);
        const int score = scoreText(title.toLower(), needle);
        if (score > 0)
            addHit(score + 10, commandRow(def));
    }

    if (m_running) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_cachedWindows.isEmpty() || (now - m_cachedWindowsTime) > 1500) {
            m_cachedWindows = m_running->allWindows();
            m_cachedWindowsTime = now;
        }
        for (const QVariant &item : m_cachedWindows) {
            const QVariantMap window = item.toMap();
            const QString title = window.value(QStringLiteral("title")).toString();
            const QString name = window.value(QStringLiteral("name")).toString();
            const int score = qMax(scoreText(title.toLower(), needle), scoreText(name.toLower(), needle) - 100);
            if (score <= 0)
                continue;

            QVariantMap row;
            row.insert(QStringLiteral("kind"), QStringLiteral("window"));
            row.insert(QStringLiteral("title"), title);
            row.insert(QStringLiteral("subtitle"),
                       tr("Switch to %1").arg(name.isEmpty() ? tr("window") : name));
            row.insert(QStringLiteral("windowId"), window.value(QStringLiteral("windowId")));
            row.insert(QStringLiteral("iconId"), window.value(QStringLiteral("iconId")));

            Hit hit;
            hit.score = score;
            hit.row = row;
            hits.append(hit); // rank first, cap later
        }
    }

    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.score > b.score; });

    QVariantList results;
    int appsAndCommands = 0;
    int windows = 0;
    constexpr int kMaxAppsAndCommands = 6;
    constexpr int kMaxWindows = 3;
    for (const Hit &hit : hits) {
        const QString kind = hit.row.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("window")) {
            if (windows >= kMaxWindows)
                continue;
            ++windows;
        } else {
            if (appsAndCommands >= kMaxAppsAndCommands)
                continue;
            ++appsAndCommands;
        }
        results.append(hit.row);
        if (results.size() >= 9)
            break;
    }

    QVariantMap web;
    web.insert(QStringLiteral("kind"), QStringLiteral("web"));
    web.insert(QStringLiteral("title"), tr("Search the web for \"%1\"").arg(text));
    web.insert(QStringLiteral("subtitle"), tr("Opens your browser"));
    web.insert(QStringLiteral("icon"), QStringLiteral("globe"));
    web.insert(QStringLiteral("query"), text);
    results.append(web);

    return results;
}

void AppSearch::activate(const QVariantMap &item)
{
    const QString kind = item.value(QStringLiteral("kind")).toString();

    if (kind == QStringLiteral("app")) {
        openPath(item.value(QStringLiteral("path")).toString());
    } else if (kind == QStringLiteral("window")) {
        if (m_running)
            m_running->activateWindow(item.value(QStringLiteral("windowId")).toDouble());
    } else if (kind == QStringLiteral("calc")) {
        const QString value = item.value(QStringLiteral("value")).toString();
        QGuiApplication::clipboard()->setText(value);
        emit toastRequested(tr("Copied %1").arg(value));
    } else if (kind == QStringLiteral("web")) {
        const QString query = item.value(QStringLiteral("query")).toString();
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=")
                                       + QString::fromUtf8(QUrl::toPercentEncoding(query))));
    } else if (kind == QStringLiteral("command")) {
        emit commandRequested(item.value(QStringLiteral("id")).toString());
    }
}

bool AppSearch::openPath(const QString &path) const
{
    if (path.isEmpty())
        return false;
#ifdef Q_OS_WIN
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<quintptr>(result) > 32;
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}
