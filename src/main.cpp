#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <QWindow>

#include "AppIconProvider.h"
#include "AppStorage.h"
#include "AppSearch.h"
#include "AppSecrets.h"
#include "Appearance.h"
#include "AudioDevices.h"
#include "BuddyProvider.h"
#include "BuddySfx.h"
#include "ChatSessions.h"
#include "ClipboardHistory.h"
#include "DeepgramAgent.h"
#include "DeepgramSettings.h"
#include "DockController.h"
#include "FullscreenWatcher.h"
#include "GlobalHotkey.h"
#include "IconSet.h"
#include "QmlSingletons.h"
#include "MediaHub.h"
#include "MusicAgentBackend.h"
#include "MusicPlayer.h"
#include "OverlayController.h"
#include "PowerActions.h"
#include "PushToTalkKey.h"
#include "RunningApps.h"
#include "SessionLink.h"
#include "SettingsRegistry.h"
#include "SmtcMedia.h"
#include "SpeechOut.h"
#include "VoiceInput.h"
#include "WakeWord.h"
#include "WindowMonitor.h"
#include "YtDlp.h"

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

    // Settings, the JSON snapshot and the secret vault share one directory.
    // This also repairs a torn settings file before anything reads it.
    noch::storage::install();

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
    // One listing of the desktop's windows, shared by the dock, the running-apps
    // tiles and the full-screen guard (see WindowMonitor.h).
    auto *windowMonitor = new WindowMonitor(&app);
    auto *dock = new DockController(windowMonitor, &app);
    auto *player = new MusicPlayer(&app);
    auto *appearance = new Appearance(&app);
    auto *buddyProvider = new BuddyProvider(&app);
    buddyProvider->startConnectors();   // saved sign-ins and MCP servers (Settings > Connectors)
    // API keys live in the same encrypted store as the connector tokens.
    // Sweep any key an older version left in the plain settings file into
    // the store (and out of the file) before QML reads a single setting.
    auto *appSecrets = new AppSecrets(buddyProvider->connectors()->secrets(), &app);
    {
        QSettings ini;
        appsecrets::migrateLegacySecrets(ini, *buddyProvider->connectors()->secrets());
        // Older versions had no "follow Buddy" choice for the live agent's
        // think model: a think configuration the user actually changed
        // becomes an explicit override, an untouched one follows Buddy.
        migrateDeepgramSettings(ini);
    }
    auto *buddySfx = new BuddySfx(&app);
    auto *chatSessions = new ChatSessions(&app);
    chatSessions->setRoot(chatSessions->sessionsPath());   // loads the saved chats
    new SessionLink(chatSessions, buddyProvider, &app);     // long chats get a rolling summary
    // The one Deepgram configuration: key (from the Secrets vault), listen
    // fields, think choice and speak voice. Push-to-talk, spoken answers
    // and the live agent all read from this object and nowhere else.
    auto *deepgramSettings = new DeepgramSettings(&app);
    auto *voiceInput = new VoiceInput(&app);
    voiceInput->setDeepgramSettings(deepgramSettings);
    auto *talkKey = new PushToTalkKey(&app);
    // "Hey buddy": listens for the phrase through the same speech service, and
    // stays quiet while Buddy talks (so it never wakes itself up).
    auto *wakeWord = new WakeWord(voiceInput, &app);
    auto *speechOut = new SpeechOut(&app);
    speechOut->setDeepgramSettings(deepgramSettings);
    // The live conversation (Deepgram Voice Agent). While it is open it owns the
    // microphone: push-to-talk and the wake word step aside so nothing else listens.
    auto *live = new DeepgramAgent(&app);
    live->setDeepgramSettings(deepgramSettings);
    const auto syncWakePause = [wakeWord, live, speechOut] {
        wakeWord->setPaused(live->active() || speechOut->speaking());
    };
    QObject::connect(speechOut, &SpeechOut::speakingChanged, wakeWord, syncWakePause);
    QObject::connect(live, &DeepgramAgent::stateChanged, wakeWord, syncWakePause);
    // Hold the key: the microphone is open; let go: what was said is transcribed.
    // With live conversation on, the same key starts and ends a conversation instead.
    QObject::connect(talkKey, &PushToTalkKey::pressed, voiceInput, [voiceInput, live] {
        if (live->enabled())
            live->toggle();
        else
            voiceInput->beginListening();
    });
    QObject::connect(talkKey, &PushToTalkKey::released, voiceInput, &VoiceInput::endListening);
    auto *clipboard = new ClipboardHistory(&app);
    auto *openApps = new RunningApps(windowMonitor, &app);
    auto *audio = new AudioDevices(&app);
    auto *smtc = new SmtcMedia(&app);
    auto *media = new MediaHub(player, smtc, audio, &app);
    // Buddy drives the music pill through this: play, pause, skip, volume, Noch's queue.
    // Songs from YouTube, found and streamed without a browser (yt-dlp): Buddy plays them in the pill.
    auto *youtube = new YtDlp(&app);
    buddyProvider->setMusicBackend(new MusicAgentBackend(media, player, youtube));
    auto *power = new PowerActions(&app);
    auto *fullscreen = new FullscreenWatcher(windowMonitor, &app);
    auto *hotkey = new GlobalHotkey(&app);
    auto *search = new AppSearch(openApps, &app);
    // The settings schema: stateless, so it can be created last.
    auto *settingsRegistry = new SettingsRegistry(&app);

    // The engine takes ownership of the providers.
    engine.addImageProvider(QStringLiteral("appicon"),
                            new AppIconProvider(dock, openApps, search));
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(player));
    engine.addImageProvider(QStringLiteral("smtccover"), new SmtcCoverProvider(smtc));
    engine.addImageProvider(QStringLiteral("preview"), new RunningPreviewProvider());
    auto *iconSet = new IconSet(&app);

    // Same instances as typed QML singletons (see QmlSingletons.h).
    OverlaySingleton::s_singletonInstance = overlay;
    DockSingleton::s_singletonInstance = dock;
    PlayerSingleton::s_singletonInstance = player;
    AppearanceSingleton::s_singletonInstance = appearance;
    IconsSingleton::s_singletonInstance = iconSet;
    BuddyAgentSingleton::s_singletonInstance = buddyProvider;
    SecretsSingleton::s_singletonInstance = appSecrets;
    AgentControlSingleton::s_singletonInstance = buddyProvider->computer();
    BuddySoundSingleton::s_singletonInstance = buddySfx;
    SessionsSingleton::s_singletonInstance = chatSessions;
    VoiceSingleton::s_singletonInstance = voiceInput;
    TalkKeySingleton::s_singletonInstance = talkKey;
    WakeSingleton::s_singletonInstance = wakeWord;
    LiveSingleton::s_singletonInstance = live;
    DeepgramSingleton::s_singletonInstance = deepgramSettings;
    YoutubeSingleton::s_singletonInstance = youtube;
    SpeechSingleton::s_singletonInstance = speechOut;
    OpenAppsSingleton::s_singletonInstance = openApps;
    ClipboardSingleton::s_singletonInstance = clipboard;
    MediaSingleton::s_singletonInstance = media;
    AudioSingleton::s_singletonInstance = audio;
    PowerSingleton::s_singletonInstance = power;
    FullscreenSingleton::s_singletonInstance = fullscreen;
    HotkeySingleton::s_singletonInstance = hotkey;
    AppIndexSingleton::s_singletonInstance = search;
    SettingsSchemaSingleton::s_singletonInstance = settingsRegistry;
    engine.loadFromModule("noch", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    overlay->setWindow(window);
    hotkey->setWindow(window);
    talkKey->setWindow(window);

    dock->syncStateSnapshot();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, dock, &DockController::syncStateSnapshot);

    return app.exec();
}
