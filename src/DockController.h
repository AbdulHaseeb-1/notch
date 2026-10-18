#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

// Owns the list of pinned apps shown in the notch: discovers common apps on
// first run, persists the list, launches entries, and tracks which of them are
// currently running.
class DockController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(QStringList running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool launchAtStartup READ launchAtStartup WRITE setLaunchAtStartup
                   NOTIFY launchAtStartupChanged)
    Q_PROPERTY(bool startupSupported READ startupSupported CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QUrl settingsFileUrl READ settingsFileUrl CONSTANT)

public:
    explicit DockController(QObject *parent = nullptr);

    QVariantList apps() const;
    QStringList running() const { return m_running; }
    bool launchAtStartup() const;
    void setLaunchAtStartup(bool enabled);
    bool startupSupported() const;
    QString version() const;
    QString dataDir() const;
    QUrl settingsFileUrl() const;

    // Thread-safe lookup used by the icon provider.
    QString iconPathForId(const QString &iconId) const;

    Q_INVOKABLE bool launch(int index);
    Q_INVOKABLE bool pinUrl(const QUrl &url);
    Q_INVOKABLE bool pinPath(const QString &path);
    Q_INVOKABLE bool pinApp(const QString &name, const QString &path);
    Q_INVOKABLE bool isPinned(const QString &path, const QString &exe = QString()) const;
    Q_INVOKABLE void unpin(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void revealInFolder(int index);
    Q_INVOKABLE void rename(int index, const QString &name);
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void openDataFolder() const;
    Q_INVOKABLE void syncStateSnapshot() const;
    Q_INVOKABLE void quit();

signals:
    void appsChanged();
    void runningChanged();
    void launchAtStartupChanged();
    void launched(int index);

private:
    struct App {
        QString name;
        QString path;       // what gets opened (.exe, .lnk, ...)
        QStringList args;   // optional launch arguments
        QString icon;       // file the icon is read from (defaults to path)
        QString exe;        // lower-case process name used for "running"
    };

    void load();
    void save() const;
    void rebuildIconIndex();
    void rebuildAppsCache();
    void pollRunning();
    QList<App> discoverDefaults() const;
    static App makeApp(const QString &path);
    static QString iconIdFor(const App &app);

    QList<App> m_apps;
    QVariantList m_appsCache;
    QStringList m_running;
    QTimer m_pollTimer;

    mutable QMutex m_iconMutex;
    QHash<QString, QString> m_iconIndex; // iconId -> icon file
};
