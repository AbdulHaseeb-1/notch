#include "DockController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSettings>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <tlhelp32.h>
#endif

namespace {

constexpr int kMaxDefaultApps = 8;
const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

QString env(const char *name)
{
    return qEnvironmentVariable(name);
}

QString joinPath(const QString &base, const QString &rest)
{
    if (base.isEmpty())
        return {};
    return QDir::cleanPath(base + QLatin1Char('/') + rest);
}

QString resolveShortcut(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("lnk"), Qt::CaseInsensitive) == 0) {
        const QString target = fi.symLinkTarget();
        if (!target.isEmpty())
            return target;
    }
    return path;
}

// Start-menu shortcuts indexed by lower-case display name, so apps that live in
// unusual install folders can still be found by name.
QHash<QString, QString> startMenuShortcuts()
{
    QHash<QString, QString> result;
#ifdef Q_OS_WIN
    const QStringList roots = {
        joinPath(env("APPDATA"), QStringLiteral("Microsoft/Windows/Start Menu/Programs")),
        joinPath(env("ProgramData"), QStringLiteral("Microsoft/Windows/Start Menu/Programs")),
    };
    for (const QString &root : roots) {
        if (root.isEmpty() || !QFileInfo::exists(root))
            continue;
        QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            const QString key = fi.completeBaseName().toLower();
            if (!result.contains(key))
                result.insert(key, fi.absoluteFilePath());
        }
    }
#endif
    return result;
}

} // namespace

DockController::DockController(QObject *parent)
    : QObject(parent)
{
    load();

    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DockController::pollRunning);
    m_pollTimer.start();
    pollRunning();
}

QString DockController::version() const
{
    return QCoreApplication::applicationVersion();
}

QVariantList DockController::apps() const
{
    return m_appsCache;
}

void DockController::rebuildAppsCache()
{
    QVariantList list;
    list.reserve(m_apps.size());
    for (const App &app : m_apps) {
        const QFileInfo fi(app.path);
        QVariantMap m;
        m.insert(QStringLiteral("name"), app.name);
        m.insert(QStringLiteral("path"), QDir::toNativeSeparators(app.path));
        m.insert(QStringLiteral("exe"), app.exe);
        m.insert(QStringLiteral("iconId"), iconIdFor(app));
        m.insert(QStringLiteral("exists"), !app.path.isEmpty() && fi.exists());
        m.insert(QStringLiteral("isFolder"), fi.isDir());
        list.append(m);
    }
    m_appsCache = list;
}

QString DockController::iconPathForId(const QString &iconId) const
{
    QMutexLocker lock(&m_iconMutex);
    return m_iconIndex.value(iconId);
}

QString DockController::iconIdFor(const App &app)
{
    const QString key = !app.icon.isEmpty() ? app.icon
                      : !app.path.isEmpty() ? app.path
                                            : QStringLiteral("name:") + app.name;
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
}

void DockController::rebuildIconIndex()
{
    {
        QMutexLocker lock(&m_iconMutex);
        m_iconIndex.clear();
        for (const App &app : std::as_const(m_apps)) {
            const QString file = !app.icon.isEmpty() ? app.icon : app.path;
            if (!file.isEmpty())
                m_iconIndex.insert(iconIdFor(app), file);
        }
    }
    rebuildAppsCache();
}

DockController::App DockController::makeApp(const QString &rawPath)
{
    App app;
    app.path = QDir::cleanPath(QDir::fromNativeSeparators(rawPath));
    const QFileInfo fi(app.path);
    app.name = fi.isDir() ? fi.fileName() : fi.completeBaseName();
    if (app.name.isEmpty())
        app.name = app.path;

    const QFileInfo target(resolveShortcut(app.path));
    if (target.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0)
        app.exe = target.fileName().toLower();
    return app;
}

QList<DockController::App> DockController::discoverDefaults() const
{
    QList<App> found;

#ifdef Q_OS_WIN
    struct Candidate {
        QString name;
        QStringList paths;
        QStringList startMenu;   // shortcut names to try if no path matched
        QStringList args = {};
        QString icon = {};
        QString exe = {};
    };

    const QString local = env("LOCALAPPDATA");
    const QString roaming = env("APPDATA");
    const QString pf = env("ProgramFiles");
    const QString pf86 = env("ProgramFiles(x86)");
    const QString windir = env("WINDIR");

    const QList<Candidate> candidates = {
        {QStringLiteral("Claude"),
         {joinPath(local, "AnthropicClaude/claude.exe"),
          joinPath(local, "Programs/Claude/Claude.exe"),
          joinPath(pf, "Claude/Claude.exe")},
         {QStringLiteral("claude")}},
        {QStringLiteral("Cursor"),
         {joinPath(local, "Programs/cursor/Cursor.exe"),
          joinPath(pf, "cursor/Cursor.exe")},
         {QStringLiteral("cursor")}},
        {QStringLiteral("VS Code"),
         {joinPath(local, "Programs/Microsoft VS Code/Code.exe"),
          joinPath(pf, "Microsoft VS Code/Code.exe")},
         {QStringLiteral("visual studio code")}},
        {QStringLiteral("Chrome"),
         {joinPath(pf, "Google/Chrome/Application/chrome.exe"),
          joinPath(pf86, "Google/Chrome/Application/chrome.exe"),
          joinPath(local, "Google/Chrome/Application/chrome.exe")},
         {QStringLiteral("google chrome")}},
        {QStringLiteral("Firefox"),
         {joinPath(pf, "Mozilla Firefox/firefox.exe")},
         {QStringLiteral("firefox")}},
        {QStringLiteral("Notion"),
         {joinPath(local, "Programs/Notion/Notion.exe")},
         {QStringLiteral("notion")}},
        {QStringLiteral("Figma"),
         {joinPath(local, "Figma/Figma.exe")},
         {QStringLiteral("figma")}},
        {QStringLiteral("Obsidian"),
         {joinPath(local, "Programs/Obsidian/Obsidian.exe"),
          joinPath(local, "Programs/obsidian/Obsidian.exe")},
         {QStringLiteral("obsidian")}},
        {QStringLiteral("Spotify"),
         {joinPath(roaming, "Spotify/Spotify.exe")},
         {QStringLiteral("spotify")}},
        {QStringLiteral("Discord"),
         {joinPath(local, "Discord/Update.exe")},
         {},
         {QStringLiteral("--processStart"), QStringLiteral("Discord.exe")},
         joinPath(local, "Discord/app.ico"),
         QStringLiteral("discord.exe")},
        {QStringLiteral("Slack"),
         {joinPath(local, "slack/slack.exe")},
         {QStringLiteral("slack")}},
        {QStringLiteral("Qt Creator"),
         {QStringLiteral("C:/Qt/Tools/QtCreator/bin/qtcreator.exe")},
         {QStringLiteral("qt creator")}},
        {QStringLiteral("Explorer"),
         {joinPath(windir, "explorer.exe")},
         {}},
    };

    const QHash<QString, QString> shortcuts = startMenuShortcuts();

    for (const Candidate &c : candidates) {
        if (found.size() >= kMaxDefaultApps)
            break;

        QString hit;
        for (const QString &p : c.paths) {
            if (!p.isEmpty() && QFileInfo::exists(p)) {
                hit = p;
                break;
            }
        }
        if (hit.isEmpty()) {
            for (const QString &s : c.startMenu) {
                if (shortcuts.contains(s)) {
                    hit = shortcuts.value(s);
                    break;
                }
            }
        }
        if (hit.isEmpty())
            continue;

        App app = makeApp(hit);
        app.name = c.name;
        app.args = c.args;
        if (!c.icon.isEmpty() && QFileInfo::exists(c.icon))
            app.icon = c.icon;
        if (!c.exe.isEmpty())
            app.exe = c.exe;
        found.append(app);
    }
#else
    // Non-Windows builds are only used for development; show placeholders so
    // the layout can be exercised.
    for (const char *name : {"Claude", "Cursor", "Terminal", "Browser", "Notes", "Files"}) {
        App app;
        app.name = QString::fromLatin1(name);
        found.append(app);
    }
#endif

    return found;
}

void DockController::load()
{
    QSettings settings;
    const QByteArray raw = settings.value(QStringLiteral("dock/apps")).toByteArray();
    const bool initialized = settings.value(QStringLiteral("dock/initialized"), false).toBool();

    m_apps.clear();
    if (initialized) {
        const QJsonArray array = QJsonDocument::fromJson(raw).array();
        for (const QJsonValue &v : array) {
            const QJsonObject o = v.toObject();
            App app;
            app.name = o.value(QStringLiteral("name")).toString();
            app.path = o.value(QStringLiteral("path")).toString();
            app.icon = o.value(QStringLiteral("icon")).toString();
            app.exe = o.value(QStringLiteral("exe")).toString();
            for (const QJsonValue &a : o.value(QStringLiteral("args")).toArray())
                app.args.append(a.toString());
            if (!app.name.isEmpty() || !app.path.isEmpty())
                m_apps.append(app);
        }
    } else {
        m_apps = discoverDefaults();
        save();
    }
    rebuildIconIndex();
}

void DockController::save() const
{
    QJsonArray array;
    for (const App &app : m_apps) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), app.name);
        o.insert(QStringLiteral("path"), app.path);
        if (!app.icon.isEmpty())
            o.insert(QStringLiteral("icon"), app.icon);
        if (!app.exe.isEmpty())
            o.insert(QStringLiteral("exe"), app.exe);
        if (!app.args.isEmpty())
            o.insert(QStringLiteral("args"), QJsonArray::fromStringList(app.args));
        array.append(o);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("dock/apps"),
                      QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("dock/initialized"), true);
}

bool DockController::launch(int index)
{
    if (index < 0 || index >= m_apps.size())
        return false;
    const App &app = m_apps.at(index);
    if (app.path.isEmpty())
        return false;

    bool ok = false;
#ifdef Q_OS_WIN
    const std::wstring file = QDir::toNativeSeparators(app.path).toStdWString();
    const QFileInfo fi(app.path);
    const std::wstring dir = QDir::toNativeSeparators(
        fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()).toStdWString();
    QStringList quoted;
    for (const QString &a : app.args)
        quoted << (a.contains(QLatin1Char(' ')) ? QLatin1Char('"') + a + QLatin1Char('"') : a);
    const std::wstring params = quoted.join(QLatin1Char(' ')).toStdWString();

    const HINSTANCE result = ShellExecuteW(nullptr, L"open", file.c_str(),
                                           params.empty() ? nullptr : params.c_str(),
                                           dir.c_str(), SW_SHOWNORMAL);
    ok = reinterpret_cast<INT_PTR>(result) > 32;
#else
    if (!app.args.isEmpty())
        ok = QProcess::startDetached(app.path, app.args, QFileInfo(app.path).absolutePath());
    else
        ok = QDesktopServices::openUrl(QUrl::fromLocalFile(app.path));
#endif

    if (ok)
        emit launched(index);
    return ok;
}

bool DockController::pinUrl(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;
    return pinPath(url.toLocalFile());
}

bool DockController::isPinned(const QString &path, const QString &exe) const
{
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QString resolved = cleanPath.isEmpty()
        ? QString()
        : QDir::cleanPath(QDir::fromNativeSeparators(resolveShortcut(cleanPath)));
    const QString lowerExe = exe.trimmed().toLower();

    for (const App &existing : m_apps) {
        const QString exClean = QDir::cleanPath(QDir::fromNativeSeparators(existing.path));
        if (!cleanPath.isEmpty() && exClean.compare(cleanPath, Qt::CaseInsensitive) == 0)
            return true;
        const QString exResolved = exClean.isEmpty()
            ? QString()
            : QDir::cleanPath(QDir::fromNativeSeparators(resolveShortcut(exClean)));
        if (!resolved.isEmpty() && exResolved.compare(resolved, Qt::CaseInsensitive) == 0)
            return true;
        if (!lowerExe.isEmpty() && !existing.exe.isEmpty()
            && existing.exe.compare(lowerExe, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool DockController::pinApp(const QString &name, const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;

    App app = makeApp(path);
    const QString trimmedName = name.trimmed();
    if (!trimmedName.isEmpty())
        app.name = trimmedName;

    if (isPinned(app.path, app.exe))
        return false;

    m_apps.append(app);
    save();
    rebuildIconIndex();
    emit appsChanged();
    pollRunning();
    return true;
}

bool DockController::pinPath(const QString &path)
{
    return pinApp(QString(), path);
}

void DockController::unpin(int index)
{
    if (index < 0 || index >= m_apps.size())
        return;
    m_apps.removeAt(index);
    save();
    rebuildIconIndex();
    emit appsChanged();
    pollRunning();
}

void DockController::move(int from, int to)
{
    if (from < 0 || from >= m_apps.size() || to < 0 || to >= m_apps.size() || from == to)
        return;
    m_apps.move(from, to);
    save();
    rebuildAppsCache();
    emit appsChanged();
}

void DockController::rename(int index, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (index < 0 || index >= m_apps.size() || trimmed.isEmpty())
        return;
    m_apps[index].name = trimmed;
    save();
    rebuildAppsCache();
    // Same as add/remove/insert: the display name helps settle icon ids, so
    // the two caches must move together.
    rebuildIconIndex();
    emit appsChanged();
}

void DockController::revealInFolder(int index)
{
    if (index < 0 || index >= m_apps.size())
        return;
    const QString path = m_apps.at(index).path;
    if (path.isEmpty())
        return;
#ifdef Q_OS_WIN
    QProcess proc;
    proc.setProgram(QStringLiteral("explorer.exe"));
    proc.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    proc.startDetached();
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void DockController::resetToDefaults()
{
    m_apps = discoverDefaults();
    save();
    rebuildIconIndex();
    emit appsChanged();
    pollRunning();
}

void DockController::quit()
{
    QCoreApplication::quit();
}

bool DockController::startupSupported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool DockController::launchAtStartup() const
{
#ifdef Q_OS_WIN
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return run.contains(QStringLiteral("Noch"));
#else
    return false;
#endif
}

void DockController::setLaunchAtStartup(bool enabled)
{
#ifdef Q_OS_WIN
    if (enabled == launchAtStartup())
        return;
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(QStringLiteral("Noch"), QLatin1Char('"') + exe + QLatin1Char('"'));
    } else {
        run.remove(QStringLiteral("Noch"));
    }
    run.sync();
    emit launchAtStartupChanged();
#else
    Q_UNUSED(enabled);
#endif
}

void DockController::pollRunning()
{
    QStringList next;
#ifdef Q_OS_WIN
    QSet<QString> processes;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry)) {
            do {
                processes.insert(QString::fromWCharArray(entry.szExeFile).toLower());
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
    }
    for (const App &app : std::as_const(m_apps)) {
        if (!app.exe.isEmpty() && processes.contains(app.exe) && !next.contains(app.exe))
            next.append(app.exe);
    }
#endif
    next.sort();
    if (next != m_running) {
        m_running = next;
        emit runningChanged();
    }
}
