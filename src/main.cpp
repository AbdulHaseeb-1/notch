#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QWindow>

#include "AppIconProvider.h"
#include "DockController.h"
#include "GlassBackdrop.h"
#include "MusicPlayer.h"
#include "OverlayController.h"
#include "SystemMonitor.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("noch"));
    app.setApplicationDisplayName(QStringLiteral("Noch"));
    app.setApplicationVersion(QStringLiteral(NOCH_VERSION));
    app.setOrganizationName(QStringLiteral("noch"));

    // One notch per user session: a second launch just exits.
    QLockFile instanceLock(QDir::temp().filePath(QStringLiteral("noch-instance.lock")));
    if (!instanceLock.tryLock(150))
        return 0;

    // Enable per-pixel alpha so the Quick window can show only the notch.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    auto *overlay = new OverlayController(&app);
    auto *dock = new DockController(&app);
    auto *monitor = new SystemMonitor(&app);
    auto *player = new MusicPlayer(&app);
    auto *glass = new GlassBackdrop(&app);

    // The engine takes ownership of the providers.
    engine.addImageProvider(QStringLiteral("appicon"), new AppIconProvider(dock));
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(player));
    engine.rootContext()->setContextProperty(QStringLiteral("overlay"), overlay);
    engine.rootContext()->setContextProperty(QStringLiteral("dock"), dock);
    engine.rootContext()->setContextProperty(QStringLiteral("monitor"), monitor);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), player);
    engine.rootContext()->setContextProperty(QStringLiteral("glassBackdrop"), glass);

    engine.loadFromModule("noch", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    overlay->setWindow(window);
    glass->setWindow(window);

    return app.exec();
}
