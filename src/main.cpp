#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QWindow>

#include "AppIconProvider.h"
#include "AppSearch.h"
#include "Appearance.h"
#include "AudioDevices.h"
#include "ClipboardHistory.h"
#include "DockController.h"
#include "FullscreenWatcher.h"
#include "GlassBackdrop.h"
#include "GlobalHotkey.h"
#include "MediaHub.h"
#include "MusicPlayer.h"
#include "NightLight.h"
#include "OverlayController.h"
#include "PowerActions.h"
#include "Radios.h"
#include "RunningApps.h"
#include "SmtcMedia.h"
#include "SystemMonitor.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("noch"));
    app.setApplicationDisplayName(QStringLiteral("Noch"));
    app.setApplicationVersion(QStringLiteral(NOCH_VERSION));
    app.setOrganizationName(QStringLiteral("noch"));

    // One notch per user: the lock file lives in the user's temp folder, so it
    // also holds across sessions (a second launch from another session of the
    // same Windows account just exits; a different user gets their own notch).
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
    auto *appearance = new Appearance(&app);
    auto *glass = new GlassBackdrop(&app);
    auto *clipboard = new ClipboardHistory(&app);
    auto *openApps = new RunningApps(&app);
    auto *audio = new AudioDevices(&app);
    auto *smtc = new SmtcMedia(&app);
    auto *media = new MediaHub(player, smtc, audio, openApps, &app);
    auto *radios = new Radios(&app);
    auto *nightLight = new NightLight(&app);
    auto *power = new PowerActions(&app);
    auto *fullscreen = new FullscreenWatcher(&app);
    auto *hotkey = new GlobalHotkey(&app);
    auto *search = new AppSearch(openApps, &app);

    // The engine takes ownership of the providers.
    engine.addImageProvider(QStringLiteral("appicon"),
                            new AppIconProvider(dock, openApps, search));
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(player));
    engine.addImageProvider(QStringLiteral("smtccover"), new SmtcCoverProvider(smtc));
    engine.addImageProvider(QStringLiteral("preview"), new RunningPreviewProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("overlay"), overlay);
    engine.rootContext()->setContextProperty(QStringLiteral("dock"), dock);
    engine.rootContext()->setContextProperty(QStringLiteral("monitor"), monitor);
    engine.rootContext()->setContextProperty(QStringLiteral("player"), player);
    engine.rootContext()->setContextProperty(QStringLiteral("appearance"), appearance);
    engine.rootContext()->setContextProperty(QStringLiteral("glassBackdrop"), glass);
    engine.rootContext()->setContextProperty(QStringLiteral("openApps"), openApps);
    engine.rootContext()->setContextProperty(QStringLiteral("clipboard"), clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("media"), media);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), audio);
    engine.rootContext()->setContextProperty(QStringLiteral("radios"), radios);
    engine.rootContext()->setContextProperty(QStringLiteral("nightLight"), nightLight);
    engine.rootContext()->setContextProperty(QStringLiteral("power"), power);
    engine.rootContext()->setContextProperty(QStringLiteral("fullscreen"), fullscreen);
    engine.rootContext()->setContextProperty(QStringLiteral("hotkey"), hotkey);
    engine.rootContext()->setContextProperty(QStringLiteral("search"), search);

    engine.loadFromModule("noch", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    overlay->setWindow(window);
    glass->setWindow(window);
    hotkey->setWindow(window);

    return app.exec();
}
