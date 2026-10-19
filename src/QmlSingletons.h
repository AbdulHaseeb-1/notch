#pragma once

// Typed QML singletons for Noch's C++ backends.
//
// main.cpp creates every backend once and also exposes it as a context
// property (kept for compatibility). These QML_FOREIGN wrappers register the
// same instances as singletons of the `noch` module, following Qt's
// "existing object as singleton" pattern. QML that uses `Media.playing`
// instead of `media.playing` gets type information, so qmllint and the QML
// compiler can check those lookups.
//
// Call NochSingletons::set(...) for each instance before loading QML.

#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include "AppSearch.h"
#include "Appearance.h"
#include "AudioDevices.h"
#include "BuddyProvider.h"
#include "BuddySfx.h"
#include "ChatSessions.h"
#include "ClipboardHistory.h"
#include "ComputerUse.h"
#include "DockController.h"
#include "FullscreenWatcher.h"
#include "GlassBackdrop.h"
#include "GlobalHotkey.h"
#include "IconSet.h"
#include "MediaHub.h"
#include "MusicPlayer.h"
#include "NightLight.h"
#include "OverlayController.h"
#include "PowerActions.h"
#include "Radios.h"
#include "RunningApps.h"

// Shared create(): hands QML the instance main.cpp made (C++ keeps ownership).
template <typename T>
T *nochSingletonCreate(T *instance, QJSEngine *engine)
{
    Q_ASSERT(instance);
    Q_UNUSED(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

// Buddy's computer-use controller (owned by BuddyProvider): drives the agent
// cursor, the screen-edge glow and the control HUD.
struct AgentControlSingleton
{
    Q_GADGET
    QML_FOREIGN(ComputerUse)
    QML_NAMED_ELEMENT(AgentControl)
    QML_SINGLETON
public:
    inline static ComputerUse *s_singletonInstance = nullptr;
    static ComputerUse *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct OverlaySingleton
{
    Q_GADGET
    QML_FOREIGN(OverlayController)
    QML_NAMED_ELEMENT(Overlay)
    QML_SINGLETON
public:
    inline static OverlayController *s_singletonInstance = nullptr;
    static OverlayController *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct DockSingleton
{
    Q_GADGET
    QML_FOREIGN(DockController)
    QML_NAMED_ELEMENT(Dock)
    QML_SINGLETON
public:
    inline static DockController *s_singletonInstance = nullptr;
    static DockController *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct PlayerSingleton
{
    Q_GADGET
    QML_FOREIGN(MusicPlayer)
    QML_NAMED_ELEMENT(Player)
    QML_SINGLETON
public:
    inline static MusicPlayer *s_singletonInstance = nullptr;
    static MusicPlayer *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct AppearanceSingleton
{
    Q_GADGET
    QML_FOREIGN(Appearance)
    QML_NAMED_ELEMENT(SystemAppearance)
    QML_SINGLETON
public:
    inline static Appearance *s_singletonInstance = nullptr;
    static Appearance *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct IconsSingleton
{
    Q_GADGET
    QML_FOREIGN(IconSet)
    QML_NAMED_ELEMENT(Icons)
    QML_SINGLETON
public:
    inline static IconSet *s_singletonInstance = nullptr;
    static IconSet *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct BuddyAgentSingleton
{
    Q_GADGET
    QML_FOREIGN(BuddyProvider)
    QML_NAMED_ELEMENT(BuddyAgent)
    QML_SINGLETON
public:
    inline static BuddyProvider *s_singletonInstance = nullptr;
    static BuddyProvider *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

// Buddy's saved chats: resume, branch, summaries.
struct SessionsSingleton
{
    Q_GADGET
    QML_FOREIGN(ChatSessions)
    QML_NAMED_ELEMENT(Sessions)
    QML_SINGLETON
public:
    inline static ChatSessions *s_singletonInstance = nullptr;
    static ChatSessions *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct BuddySoundSingleton
{
    Q_GADGET
    QML_FOREIGN(BuddySfx)
    QML_NAMED_ELEMENT(BuddySound)
    QML_SINGLETON
public:
    inline static BuddySfx *s_singletonInstance = nullptr;
    static BuddySfx *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct BackdropSingleton
{
    Q_GADGET
    QML_FOREIGN(GlassBackdrop)
    QML_NAMED_ELEMENT(Backdrop)
    QML_SINGLETON
public:
    inline static GlassBackdrop *s_singletonInstance = nullptr;
    static GlassBackdrop *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct OpenAppsSingleton
{
    Q_GADGET
    QML_FOREIGN(RunningApps)
    QML_NAMED_ELEMENT(OpenApps)
    QML_SINGLETON
public:
    inline static RunningApps *s_singletonInstance = nullptr;
    static RunningApps *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct ClipboardSingleton
{
    Q_GADGET
    QML_FOREIGN(ClipboardHistory)
    QML_NAMED_ELEMENT(Clipboard)
    QML_SINGLETON
public:
    inline static ClipboardHistory *s_singletonInstance = nullptr;
    static ClipboardHistory *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct MediaSingleton
{
    Q_GADGET
    QML_FOREIGN(MediaHub)
    QML_NAMED_ELEMENT(Media)
    QML_SINGLETON
public:
    inline static MediaHub *s_singletonInstance = nullptr;
    static MediaHub *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct AudioSingleton
{
    Q_GADGET
    QML_FOREIGN(AudioDevices)
    QML_NAMED_ELEMENT(Audio)
    QML_SINGLETON
public:
    inline static AudioDevices *s_singletonInstance = nullptr;
    static AudioDevices *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct RadiosSingleton
{
    Q_GADGET
    QML_FOREIGN(Radios)
    QML_NAMED_ELEMENT(Radio)
    QML_SINGLETON
public:
    inline static Radios *s_singletonInstance = nullptr;
    static Radios *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct NightLightSingleton
{
    Q_GADGET
    QML_FOREIGN(NightLight)
    QML_NAMED_ELEMENT(Night)
    QML_SINGLETON
public:
    inline static NightLight *s_singletonInstance = nullptr;
    static NightLight *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct PowerSingleton
{
    Q_GADGET
    QML_FOREIGN(PowerActions)
    QML_NAMED_ELEMENT(Power)
    QML_SINGLETON
public:
    inline static PowerActions *s_singletonInstance = nullptr;
    static PowerActions *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct FullscreenSingleton
{
    Q_GADGET
    QML_FOREIGN(FullscreenWatcher)
    QML_NAMED_ELEMENT(Fullscreen)
    QML_SINGLETON
public:
    inline static FullscreenWatcher *s_singletonInstance = nullptr;
    static FullscreenWatcher *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct HotkeySingleton
{
    Q_GADGET
    QML_FOREIGN(GlobalHotkey)
    QML_NAMED_ELEMENT(Hotkey)
    QML_SINGLETON
public:
    inline static GlobalHotkey *s_singletonInstance = nullptr;
    static GlobalHotkey *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};

struct AppIndexSingleton
{
    Q_GADGET
    QML_FOREIGN(AppSearch)
    QML_NAMED_ELEMENT(AppIndex)
    QML_SINGLETON
public:
    inline static AppSearch *s_singletonInstance = nullptr;
    static AppSearch *create(QQmlEngine *, QJSEngine *engine) { return nochSingletonCreate(s_singletonInstance, engine); }
};
