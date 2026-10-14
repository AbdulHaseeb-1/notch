#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class DockController;
class RunningApps;

// Serves "image://appicon/<mono|color>/<iconId>": the real icon of an installed
// app or shortcut, read from the file itself, optionally rendered in
// black-and-white.
class AppIconProvider final : public QQuickImageProvider
{
public:
    // Pinned apps are looked up first, then the programs that are open now.
    AppIconProvider(const DockController *dock, const RunningApps *running);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage loadIcon(const QString &file, int extent);
    static QImage toMonochrome(const QImage &source);

    const DockController *m_dock;
    const RunningApps *m_running;
    QMutex m_mutex;
    QHash<QString, QImage> m_cache;     // file -> colour icon
    QHash<QString, QImage> m_monoCache; // file -> monochrome icon
};
