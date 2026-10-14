#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>
#include <QVariantList>

// The programs that have a window open right now, grouped per program. This is
// what is open; DockController holds what is pinned, and the two stay apart.
class RunningApps final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)

public:
    explicit RunningApps(QObject *parent = nullptr);

    // One entry per program: name, exe (lower-case file name), path, iconId, count.
    QVariantList apps() const { return m_apps; }

    // Thread-safe lookup used by the icon provider.
    QString iconPathForId(const QString &iconId) const;

    // The windows of one program, topmost first: windowId and title.
    Q_INVOKABLE QVariantList windowsFor(const QString &exe) const;
    Q_INVOKABLE bool activateApp(const QString &exe);
    Q_INVOKABLE bool activateWindow(double windowId);

signals:
    void appsChanged();

private:
    void refresh();

    QVariantList m_apps;
    QString m_signature;
    QTimer m_timer;
    mutable QMutex m_iconMutex;
    QHash<QString, QString> m_iconIndex; // iconId -> program path
};

// Serves "image://preview/<windowId>/<stamp>": a snapshot of an open window.
// The stamp only forces a fresh capture each time the preview opens.
class RunningPreviewProvider final : public QQuickImageProvider
{
public:
    RunningPreviewProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
