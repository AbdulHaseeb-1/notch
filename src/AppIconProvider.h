#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class AppSearch;
class DockController;
class RunningApps;

// Serves "image://appicon/<mono|monoLight|color>/<iconId>": the real icon of an
// installed app or shortcut, read from the file itself. The mono variants are
// black-and-white, light for the dark notch and dark for the light theme.
class AppIconProvider final : public QQuickImageProvider
{
public:
    // Pinned apps are looked up first, then the programs that are open now,
    // then the Start Menu apps the command palette searches.
    AppIconProvider(const DockController *dock, const RunningApps *running,
                    const AppSearch *search);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QImage loadIcon(const QString &file, int extent);
    QImage colourIcon(const QString &file);
    static QImage toMonochrome(const QImage &source);
    static QImage toInkMonochrome(const QImage &source);

    const DockController *m_dock;
    const RunningApps *m_running;
    const AppSearch *m_search;
    QMutex m_mutex;
    QHash<QString, QImage> m_cache;     // file -> colour icon
    QHash<QString, QImage> m_monoCache; // file -> light monochrome icon (dark notch)
    QHash<QString, QImage> m_inkCache;  // file -> dark monochrome icon (light theme)
};
