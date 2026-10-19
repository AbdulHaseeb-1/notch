#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

#include "AppIconProvider.h"
#include "AppSearch.h"
#include "Appearance.h"
#include "AudioDevices.h"
#include "BuddyProvider.h"
#include "BuddySfx.h"
#include "ClipboardHistory.h"
#include "DockController.h"
#include "FullscreenWatcher.h"
#include "GlassBackdrop.h"
#include "GlobalHotkey.h"
#include "IconSet.h"
#include "QmlSingletons.h"
#include "MediaHub.h"
#include "MusicPlayer.h"
#include "NightLight.h"
#include "OverlayController.h"
#include "PowerActions.h"
#include "Radios.h"
#include "RunningApps.h"
#include "SmtcMedia.h"

namespace {

void initializeSafeStorage()
{
    // Persist all settings and app data to %APPDATA%/noch/noch.ini (and a
    // human-readable JSON backup noch-state.json) instead of only the Registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QString configRoot = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
    if (configRoot.isEmpty())
        configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!configRoot.isEmpty())
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configRoot);

    QSettings ini;
    const QString dataDir = QFileInfo(ini.fileName()).absolutePath();
    QDir().mkpath(dataDir);

    // 1. One-time migration of any existing settings from the Windows Registry.
    if (!ini.value(QStringLiteral("meta/migratedFromRegistry"), false).toBool()) {
        QSettings reg(QSettings::NativeFormat, QSettings::UserScope,
                      QStringLiteral("noch"), QStringLiteral("noch"));
        const QStringList regKeys = reg.allKeys();
        for (const QString &key : regKeys) {
            if (!ini.contains(key))
                ini.setValue(key, reg.value(key));
        }
        ini.setValue(QStringLiteral("meta/migratedFromRegistry"), true);
        ini.sync();
    }

    // 2. If the INI file was ever deleted/reset, restore from noch-state.json if present.
    const QString snapshotPath = QDir(dataDir).filePath(QStringLiteral("noch-state.json"));
    if (!ini.value(QStringLiteral("dock/initialized"), false).toBool() && QFileInfo::exists(snapshotPath)) {
        QFile f(snapshotPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject savedSettings = root.value(QStringLiteral("settings")).toObject();
            for (auto it = savedSettings.begin(); it != savedSettings.end(); ++it) {
                if (it.value().isArray()) {
                    QStringList list;
                    for (const QJsonValue &v : it.value().toArray())
                        list.append(v.toString());
                    ini.setValue(it.key(), list);
                } else {
                    ini.setValue(it.key(), it.value().toVariant());
                }
            }
            ini.sync();
        }
    }
}

} // namespace

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

    initializeSafeStorage();

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
    auto *player = new MusicPlayer(&app);
    auto *appearance = new Appearance(&app);
    auto *buddyProvider = new BuddyProvider(&app);
    auto *buddySfx = new BuddySfx(&app);
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
    engine.rootContext()->setContextProperty(QStringLiteral("player"), player);
    engine.rootContext()->setContextProperty(QStringLiteral("appearance"), appearance);
    auto *iconSet = new IconSet(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("iconSet"), iconSet);
    engine.rootContext()->setContextProperty(QStringLiteral("buddyProvider"), buddyProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("buddySfx"), buddySfx);
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

    // Same instances as typed QML singletons (see QmlSingletons.h).
    OverlaySingleton::s_singletonInstance = overlay;
    DockSingleton::s_singletonInstance = dock;
    PlayerSingleton::s_singletonInstance = player;
    AppearanceSingleton::s_singletonInstance = appearance;
    IconsSingleton::s_singletonInstance = iconSet;
    BuddyAgentSingleton::s_singletonInstance = buddyProvider;
    AgentControlSingleton::s_singletonInstance = buddyProvider->computer();
    BuddySoundSingleton::s_singletonInstance = buddySfx;
    BackdropSingleton::s_singletonInstance = glass;
    OpenAppsSingleton::s_singletonInstance = openApps;
    ClipboardSingleton::s_singletonInstance = clipboard;
    MediaSingleton::s_singletonInstance = media;
    AudioSingleton::s_singletonInstance = audio;
    RadiosSingleton::s_singletonInstance = radios;
    NightLightSingleton::s_singletonInstance = nightLight;
    PowerSingleton::s_singletonInstance = power;
    FullscreenSingleton::s_singletonInstance = fullscreen;
    HotkeySingleton::s_singletonInstance = hotkey;
    AppIndexSingleton::s_singletonInstance = search;
    engine.loadFromModule("noch", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    overlay->setWindow(window);
    glass->setWindow(window);
    hotkey->setWindow(window);

    dock->syncStateSnapshot();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, dock, &DockController::syncStateSnapshot);

    return app.exec();
}
