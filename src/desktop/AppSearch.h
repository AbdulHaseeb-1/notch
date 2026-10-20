#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>

class RunningApps;

// The command palette's brain: an index of Start Menu applications, fuzzy
// matching, the inline calculator, open-window switching, a web search
// fallback and a handful of built-in commands. The QML side owns the looks;
// this owns finding and doing.
class AppSearch final : public QObject
{
    Q_OBJECT
    // True while the background Start Menu scan is still filling the index.
    // Callers that need a definitive "not installed" answer can wait for
    // indexReady instead of treating a cold index as no match.
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexReady)

public:
    explicit AppSearch(RunningApps *running, QObject *parent = nullptr);
    ~AppSearch() override;

    // Ranked results for the query: calc, apps, commands, windows, web.
    Q_INVOKABLE QVariantList search(const QString &query);

    bool indexing() const { return m_scanThread != nullptr; }

    // Installed and running applications for the "Add apps" list in Settings.
    Q_INVOKABLE QVariantList availableApps(const QString &query = QString());
    Q_INVOKABLE void activate(const QVariantMap &item);
    // Launches a file, shortcut or anything ShellExecute understands.
    Q_INVOKABLE bool openPath(const QString &path) const;

    // Icon lookup for the app icon provider, mirroring DockController's.
    QString iconPathForId(const QString &iconId) const;

signals:
    // A built-in command the QML side performs (lock, sleep, settings, ...).
    void commandRequested(const QString &command);
    void toastRequested(const QString &text);
    // A search kicked off while the Start Menu scan is still running can
    // refresh once the background index has landed.
    void indexReady();

private:
    struct Entry {
        QString name;
        QString lowerName;
        QString path;
        QString exe;
        QString lowerExe;
        QString iconId;
    };

    void ensureIndex();
    void applyIndex(QList<Entry> entries, QHash<QString, QString> icons);

    RunningApps *m_running = nullptr;
    std::atomic<bool> m_indexed{false};
    QList<Entry> m_entries;
    mutable QMutex m_iconMutex;
    QHash<QString, QString> m_icons; // iconId -> file
    QThread *m_scanThread = nullptr;

    mutable QVariantList m_cachedWindows;
    mutable qint64 m_cachedWindowsTime = 0;
};
