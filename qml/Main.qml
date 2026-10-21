pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtCore

Window {
    id: root
    // Handles for the flyouts, which used to reach these ids through the
    // parent context. Qualified access keeps qmllint (and readers) happy.
    readonly property var settingsStore: cfg
    readonly property var hubTimer: sessionTimer

    visible: true
    color: "transparent"
    title: qsTr("Noch")
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height


    Settings {
        id: cfg
        location: Dock.settingsFileUrl
        category: "ui"
        property real sizeScale: 1.0
        property bool autoHide: true
        property int hideDelay: 1200
        property int focusMinutes: 25
        property bool hotkeyPalette: true
        property bool suppressInFullscreen: true
        property bool buddyEnabled: true
        property bool buddySolidRing: true
        // ---- Look & glow --------------------------------------------------------
        property string buddyRingStyle: "neon"
        property real buddyRingThickness: 1.0
        property real buddyRingSpeed: 1.0
        property real buddyGlowStrength: 0.6
        property string buddyGlowMode: "bloom"
        property bool buddyGlowAlways: false
        property string buddyVoiceFx: "full"
        property real buddyX: -1
        property real buddyY: -1
        property string buddyPlan: ""
        // Past questions and answers, newest first (JSON, capped in Buddy.qml).
        property string buddyHistory: "[]"
        // ---- Desktop buddy AI provider ---------------------------------------
        // "none" = demo fixtures only. "openai"/"custom"/"opencode" = OpenAI-compatible
        // chat/completions endpoint. "ollama" = local Ollama. "anthropic" =
        // Anthropic Messages API.
        property string buddyProvider: "none"
        property string buddyApiKey: ""
        property string buddyApiBase: ""
        property string buddyModel: "gpt-4o-mini"
        property real buddyTemperature: 0.7
        // Every provider's own {key, base, model, models} as JSON, so several
        // providers can be configured at once and switching loses nothing.
        property string buddyProviderConfigs: "{}"
        // ---- Anthropic Agent SDK & Computer Use tools ------------------------
        property bool buddyAgentMode: true
        property bool buddyAllowBash: true
        // Computer use (mouse/keyboard control) is off until the user opts in.
        property bool buddyAllowComputer: false
        // How computer use looks while Buddy is in control.
        property bool buddyCuShowCursor: true
        property bool buddyCuShowGlow: true
        property real buddyCuGlowIntensity: 0.6
        property string buddyCuGlowStyle: "soft"
        property bool buddyAllowEditor: true
        property bool buddyAllowCodeSearch: true
        property bool buddyAllowMemory: true
        property bool buddyAllowWebSearch: true
        property bool buddyAllowToolSearch: true
        property bool buddyAllowAdvisor: true
        property bool buddyAllowTodoTask: true
        // P6 tool diet: only core tools upfront, rest via tool_search.
        // Default ON: ~25% fewer tokens per request, and the diet:not-loaded
        // guardrail teaches the model how to load deferred tools.
        property bool buddyToolDiet: true
        // Claude's own computer-use tools (no extra fee; they add about 4,500
        // input tokens per request, only while computer use is on).
        property bool buddyOfficialComputer: true
        // "sharp" sends bigger screenshots (more tokens); "standard" the smaller ones.
        property string buddyComputerDetail: "sharp"
        property int buddyMaxSteps: 40
        property bool buddyAllowAskUser: true
        property bool buddyAllowScreenshots: true
        property bool buddyAllowMusic: true
        // ---- Push-to-talk voice (off until the user adds a speech service) --
        property bool buddyVoiceEnabled: false
        property bool buddyVoiceHotkey: true
        property bool buddyVoiceAutoSend: false
        // ---- Wake word ("hey buddy"): off until switched on ------------------
        property bool buddyWakeEnabled: false
        property string buddyWakePhrase: "hey buddy"
        // Everything the microphone hears is sent to the speech service to check
        // for the phrase: a service on the internet needs this explicit yes.
        // How sure the local model has to be (0.2 easy .. 0.9 strict); 0.5 is its own default.
        property real buddyWakeThreshold: 0.5
        property bool buddySpeakReports: false
        // ---- Spoken answers: "system" | "google" | "kokoro" -------------------
        property string buddyTtsEngine: "system"
        property string buddyTtsGoogleKey: ""
        property string buddyTtsGoogleVoice: "Kore"
        property string buddyTtsKokoroBase: "http://127.0.0.1:8880"
        property string buddyTtsKokoroVoice: "af_heart"
        property real buddyTtsSpeed: 1.0
        property bool buddyTtsSpeakAll: false
        // ---- Deepgram: one API key for the live conversation, speech to text and the voice ----
        property string buddyDgKey: ""
        // Speech to text for push-to-talk and the wake word: "local" (on this computer) or "deepgram".
        property string buddySttEngine: "local"
        // Live conversation (Deepgram Voice Agent): listen / think / speak, all adjustable.
        // Where yt-dlp is (for songs from YouTube); "" = look in the usual places.
        property string buddyYtDlpPath: ""
        property bool buddyLiveEnabled: false
        property string buddyLiveListenModel: "nova-3"
        property string buddyLiveLanguage: "en"
        // Names the recognizer cannot know; add the apps you open most.
        property string buddyLiveKeyterms: "Buddy, Noch, Notepad"
        property real buddyLiveEot: 0.7
        property string buddyLiveProvider: "anthropic"
        property string buddyLiveModel: "claude-haiku-5-5"
        // -1 = the model's own default (newer models refuse the setting).
        property real buddyLiveTemperature: -1
        property string buddyLivePrompt: ""
        property string buddyLiveEndpoint: ""
        property string buddyLiveEndpointKey: ""
        property bool buddyLiveAskBuddy: true
        property string buddyLiveVoice: "aura-2-thalia-en"
        property real buddyLiveSpeed: 1.0
        property real buddyLiveVolume: 1.0
        property string buddyLiveGreeting: ""
        property string buddyLiveAudioMode: "speakers"
        // After Buddy answers: keep listening for a follow-up this many seconds; 0 = not at all
        // (the wake word starts the next conversation).
        property int buddyLiveFollowUp: 30
        // ---- Agent safety (see docs/agent-safety.md) -------------------------
        // "default" asks before side effects, "acceptEdits" auto-approves file
        // edits inside the workspace, "plan" is read-only, "bypass" trusts all
        // (hard-deny rules still apply).
        property string buddyPermissionMode: "default"
        property string buddyWorkspaceRoot: ""
        property bool buddyRedactSecrets: true
        property bool buddyKeepTraces: true
        property bool buddyStreamReplies: true
        // Show what Buddy is doing while it works (steps, draft, reasoning).
        property bool buddyShowThinking: true
        property bool buddyHandoffs: true
        // JEV: a small decision model labels each request (intent, risk, effort,
        // specialist) before the agent starts. Off by default: the request text goes
        // to this endpoint, which is separate from the chat provider. An address
        // ending in /systemone or /decisions is a decision model (yes/no questions);
        // any other address is treated as a chat endpoint.
        property bool buddyClassifierEnabled: false
        property string buddyClassifierBase: "https://openrouter.ai/api/v1/systemone"
        property string buddyClassifierKey: ""
        property string buddyClassifierModel: "inception/mercury-decide:free"
        // ---- Desktop buddy sound effects -------------------------------------
        property bool buddySfxEnabled: true
        property real buddySfxVolume: 0.75
        property string buddySfxStyle: "warm"
    }

    readonly property string snapshotKey: [
        cfg.sizeScale, cfg.autoHide, cfg.hideDelay,
        cfg.focusMinutes, cfg.hotkeyPalette, cfg.suppressInFullscreen,
        cfg.buddyEnabled, cfg.buddySolidRing, Math.round(cfg.buddyX), Math.round(cfg.buddyY),
        cfg.buddyPlan.length, cfg.buddyHistory.length, cfg.buddyProvider,
        cfg.buddyApiBase, cfg.buddyModel, cfg.buddyTemperature,
        cfg.buddyAgentMode, cfg.buddyAllowBash, cfg.buddyAllowComputer, cfg.buddyCuShowCursor, cfg.buddyCuShowGlow, cfg.buddyCuGlowIntensity, cfg.buddyCuGlowStyle, cfg.buddyRingStyle, cfg.buddyRingThickness, cfg.buddyRingSpeed, cfg.buddyGlowStrength, cfg.buddyGlowMode, cfg.buddyGlowAlways, cfg.buddyVoiceFx, cfg.buddyAllowEditor,
        cfg.buddyAllowCodeSearch, cfg.buddyAllowMemory, cfg.buddyAllowWebSearch,
        cfg.buddyAllowToolSearch, cfg.buddyAllowAdvisor, cfg.buddyAllowTodoTask,
        cfg.buddyOfficialComputer, cfg.buddyComputerDetail, cfg.buddyMaxSteps, cfg.buddyAllowAskUser, cfg.buddyAllowScreenshots, cfg.buddyAllowMusic,
        cfg.buddyVoiceEnabled, cfg.buddyVoiceHotkey, cfg.buddyVoiceAutoSend,
        cfg.buddyWakeEnabled, cfg.buddyWakePhrase, cfg.buddyWakeThreshold, cfg.buddySpeakReports,
        cfg.buddyLiveEnabled, cfg.buddySttEngine, cfg.buddyLiveListenModel, cfg.buddyLiveLanguage, cfg.buddyLiveProvider, cfg.buddyLiveModel,
        cfg.buddyLiveVoice, cfg.buddyLiveSpeed, cfg.buddyLiveVolume, cfg.buddyLiveAudioMode, cfg.buddyLiveFollowUp, cfg.buddyLiveAskBuddy,
        cfg.buddyTtsEngine, cfg.buddyTtsGoogleKey, cfg.buddyTtsGoogleVoice, cfg.buddyTtsKokoroBase, cfg.buddyTtsKokoroVoice, cfg.buddyTtsSpeed, cfg.buddyTtsSpeakAll,
        cfg.buddyPermissionMode, cfg.buddyWorkspaceRoot, cfg.buddyRedactSecrets, cfg.buddyKeepTraces, cfg.buddyStreamReplies, cfg.buddyShowThinking, cfg.buddyHandoffs,
        cfg.buddyClassifierEnabled, cfg.buddyClassifierBase, cfg.buddyClassifierModel,
        cfg.buddySfxEnabled, cfg.buddySfxVolume, cfg.buddySfxStyle
    ].join("|")
    onSnapshotKeyChanged: snapshotSyncTimer.restart()
    Timer {
        id: snapshotSyncTimer
        interval: 600
        onTriggered: Dock.syncStateSnapshot()
    }

    // ------------------------------------------------------------------ size
    // `s` follows the saved size, animated unless the user is dragging.
    property real s: Math.max(0.7, Math.min(1.6, cfg.sizeScale))
    Behavior on s {
        enabled: !root.resizing
        NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
    }

    readonly property real pillH: Math.round(50 * s)
    readonly property real musicGap: Math.round(8 * s)
    readonly property real topGap: 8
    readonly property real dashW: Math.round(44 * s)
    readonly property real dashH: Math.max(4, Math.round(4 * s))
    // The music pill is all that is left of the notch; it sits in the middle of the top edge.
    readonly property real pillW: musicDock.width

    // It is only there while music is: nothing to show, nothing on screen. After the music
    // stops it stays a little while (so a pause can be undone), and it is always there while
    // it is being used, a file is dragged over the top edge, or Buddy is switched off (then it
    // is the only way into Settings).
    property bool musicHold: false
    readonly property bool musicPlaying: Media.playing
    onMusicPlayingChanged: {
        if (musicPlaying) {
            musicHold = true
            musicHoldTimer.stop()
        } else {
            musicHoldTimer.restart()
        }
    }
    Timer { id: musicHoldTimer; interval: 20000; onTriggered: root.musicHold = false }
    readonly property bool musicShown: musicPlaying || musicHold || nowPlaying.barOpen
                                       || musicPillHover.hovered || dragging || !cfg.buddyEnabled
    property real musicFade: musicShown ? 1 : 0
    Behavior on musicFade { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    // Something at the top edge worth revealing at all.
    readonly property bool notchHasContent: musicShown || timerPill.visible
    readonly property real shownY: topGap
    readonly property real hiddenY: -pillH - 4

    function setScale(v) {
        cfg.sizeScale = Math.round(Math.max(0.7, Math.min(1.6, v)) * 100) / 100
        hudTimer.restart()
    }

    // ---------------------------------------------------------- reveal logic
    property bool panelOpen: false
    property bool hubOpen: false
    property bool paletteOpen: false
    property int hubTab: 0
    property bool resizing: false
    property bool introHold: true
    property bool revealed: true

    // While a full-screen app (a game, a video, a presentation) owns the
    // screen, hover is ignored so the notch never drops in over it. The hotkey
    // still works: opening the palette makes Noch the foreground window, which
    // lifts the guard by itself.
    // When tucked away (!revealed), ONLY hovering the dash (peekZone) brings
    // the notch out — hovering around it does nothing.
    readonly property bool fullscreenGuard: cfg.suppressInFullscreen && Fullscreen.active && !paletteOpen
    // The music pill stays out on a clear desktop and tucks itself away while an app
    // window sits in its space (or a full-screen app owns the screen). Hovering the
    // dash at the top edge still pulls it out over an app.
    readonly property bool desktopClear: !fullscreenGuard && !Fullscreen.occupied
    readonly property bool notchElementsHovered: grip.containsMouse || panelItem.hovered
                                                 || hubFlyout.hovered || musicPillHover.hovered
                                                 || timerPill.hovered
                                                 || paletteFlyout.hovered
    readonly property bool pointerInside: !fullscreenGuard
                                          && (peekZone.containsMouse || (revealed && notchElementsHovered))
    readonly property bool dragging: peekZone.containsDrag || musicDrop.containsDrag
    readonly property bool holdOpen: !cfg.autoHide || panelOpen || hubOpen || paletteOpen
                                     || resizing || introHold
                                     || dragging || timerAlert
    readonly property bool wantVisible: pointerInside || holdOpen || desktopClear

    onWantVisibleChanged: syncReveal()

    function syncReveal() {
        if (wantVisible) {
            hideTimer.stop()
            if (!revealed)
                revealed = true
        } else {
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: cfg.hideDelay
        onTriggered: if (!root.wantVisible && root.revealed) root.revealed = false
    }
    Timer { running: true; interval: 140; onTriggered: root.syncReveal() }         // drop-in on launch
    Timer { running: true; interval: 2200; onTriggered: root.introHold = false }    // stay a moment, then tuck away

    // 0 = tucked away, 1 = fully shown. Smooth quintic slide out from the dash
    // with zero overshoot.
    property real progress: revealed ? 1 : 0
    Behavior on progress {
        NumberAnimation {
            duration: root.revealed ? 300 : 220
            easing.type: root.revealed ? Easing.OutQuint : Easing.InCubic
        }
    }
    readonly property bool fullyHidden: !revealed && progress <= 0.001

    function clamp01(v) { return Math.max(0, Math.min(1, v)) }

    // ------------------------------------------------------------ tooltip
    property real tipX: 0
    property bool tipReady: false
    property string toast: ""
    property string hoverHint: ""
    readonly property string tipText: toast.length > 0 ? toast : hoverHint
    readonly property bool tipVisible: revealed && !resizing && !panelOpen
                                       && !hubOpen && !paletteOpen
                                       && tipText.length > 0
                                       && (toast.length > 0 || tipReady)

    Timer { id: tipTimer; interval: 380; onTriggered: root.tipReady = true }
    Timer { id: tipGrace; interval: 160; onTriggered: root.tipReady = false }
    Timer { id: toastTimer; interval: 1300; onTriggered: root.toast = "" }
    Timer { id: hudTimer; interval: 900 }
    readonly property bool hudVisible: resizing || hudTimer.running

    // Closing the app's own surfaces. This is what empty-notch clicks and Esc
    // reach, so it tucks the buddy away instead of dismissing it: a reply still being made keeps running and pops back when ready.
    function closeAll() {
        panelOpen = false
        hubOpen = false
        paletteOpen = false
        buddy.stash()
    }

    // Opens the hub on a page (or closes it if that page is already showing).
    // tab -1 means "wherever it was last".
    function toggleHub(tab) {
        panelOpen = false
        paletteOpen = false
        const page = tab < 0 ? hubTab : tab
        if (hubOpen && hubTab === page) {
            hubOpen = false
        } else {
            hubTab = page
            hubOpen = true
        }
    }

    function openHub(tab) {
        panelOpen = false
        paletteOpen = false
        if (!revealed)
            revealed = true
        hubTab = tab
        hubOpen = true
    }

    function toggleSettings() {
        hubOpen = false
        paletteOpen = false
        if (!panelOpen && !revealed)
            revealed = true
        panelOpen = !panelOpen
    }

    // ------------------------------------------------------------ palette
    // Ctrl+Alt+Space from anywhere: drop the notch and open the palette over
    // it, with the field ready for typing.
    function summonPalette() {
        console.log("noch qml: summonPalette")
        panelOpen = false
        hubOpen = false
        if (!revealed)
            revealed = true
        paletteOpen = true
        root.requestActivate()
        paletteFlyout.takeFocus()
    }

    function closePalette() {
        paletteOpen = false
    }

    // Tells the user which shortcut the palette listens on when the preferred
    // one was already taken at startup.
    function hotkeyNotice() {
        if (!cfg.hotkeyPalette)
            return
        if (!Hotkey.registered)
            showToast(qsTr("Palette hotkey unavailable"))
        else if (!Hotkey.preferred)
            showToast(qsTr("Ctrl+Alt+Space is taken — press %1 for the palette").arg(Hotkey.shortcut))
    }

    // The built-in commands the palette's list can offer.
    function runCommand(id) {
        switch (id) {
        case "lock":
            Power.lock()
            break
        case "sleep":
            Power.sleep()
            break
        case "mic":
            Audio.toggleMicMute()
            showToast(Audio.micMuted ? qsTr("Microphone muted") : qsTr("Microphone live"))
            break
        case "taskmgr":
            AppIndex.openPath("taskmgr.exe")
            break
        case "winsettings":
            Qt.openUrlExternally("ms-settings:")
            break
        case "timer":
            openHub(0)
            break
        case "clipboard":
            openHub(1)
            break
        case "noch":
            toggleSettings()
            break
        }
    }

    function showToast(text) {
        toast = text
        toastTimer.restart()
    }

    // Clicking anywhere outside Noch must not cut a reply off: the flyouts
    // close, but the buddy only tucks its bubble away and keeps working — the
    // orb stays lit and pops the answer back up when it lands.
    onActiveChanged: if (!active) {
        panelOpen = false
        hubOpen = false
        paletteOpen = false
        // An approval or a question stays up: the agent is stopped on it, and the user
        // may well be in another app (or not looking at all, in a voice conversation).
        if (buddy.waitingOnUser)
            buddy.closeMenu()
        else
            buddy.stash()
    }

    Shortcut { sequence: "Esc"; onActivated: root.closeAll() }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.toggleSettings() }

    // ------------------------------------------------------------ mask
    // Only the visible parts of the window take the mouse; everything else
    // clicks through to the desktop.
    property real menuX: 0
    readonly property bool panelVisible: panelItem.shown
    readonly property bool hubVisible: hubFlyout.shown
    readonly property bool paletteVisible: paletteFlyout.shown
    // Flyout sizes for the mask are the sizes they are heading for, not the
    // animated ones, so the mask is not rebuilt on every frame of an animation.
    readonly property real panelTargetH: panelItem.targetHeight
    readonly property real hubTargetH: hubFlyout.targetHeight
    readonly property real paletteTargetH: paletteFlyout.targetHeight
    // The mask key is a STRING, not an array: Qt compares property values with
    // strict equality, and a fresh JS array never equals the previous one, so
    // every single dependency change (including no-op hover ticks) re-fired
    // onMaskKeyChanged and queued a full Win32 region rebuild. Joined, the
    // comparison actually succeeds and identical states cost nothing.
    readonly property bool buddyInteracting: buddy.dragging || buddyPanel.interacting
    readonly property string maskKey: [revealed, fullyHidden, Math.round(pillH), width,
                                    Math.round(musicDock.x), Math.round(musicDock.width), Math.round(timerPill.width),
                                    musicShown, Math.round(timerPill.x),
                                    panelVisible, Math.round(panelTargetH),
                                    hubVisible, Math.round(hubTargetH),
                                    paletteVisible, Math.round(paletteFlyout.width),
                                    Math.round(paletteTargetH),
                                    tipVisible, hudVisible, tip.opacity > 0.01,
                                    Math.round(tip.targetX), Math.round(tip.targetWidth),
                                    cfg.buddyEnabled, buddy.session, buddyInteracting,
                                    buddyInteracting ? 0 : Math.round(buddy.x),
                                    buddyInteracting ? 0 : Math.round(buddy.y),
                                    Math.round(buddy.width), Math.round(buddy.height),
                                    buddyInteracting ? "" : buddy.maskSig,
                                    buddyPanel.open, buddyPanel.visible,
                                    buddyInteracting ? 0 : Math.round(buddyPanel.dragX),
                                    buddyInteracting ? 0 : Math.round(buddyPanel.dragY),
                                    Math.round(buddyPanel.width),
                                    buddyInteracting ? 0 : Math.round(buddyPanel.height)].join(",")
    onMaskKeyChanged: Qt.callLater(updateMask)

    // The buddy's regions exist whether or not the notch is tucked away.
    // While dragging the buddy or its panel, expand to the full window rect so
    // Win32 SetWindowRgn is called only once at drag start and once on release.
    function pushBuddyRegions(r) {
        if (!cfg.buddyEnabled || !buddy.visible)
            return
        if (buddyInteracting) {
            r.push({ x: 0, y: 0, width: root.width, height: root.height, radius: 0 })
            return
        }
        const br = buddy.regionsIn(root.contentItem)
        for (let i = 0; i < br.length; ++i)
            r.push(br[i])
        if (buddyPanel.open || buddyPanel.visible) {
            r.push({ x: Math.round(buddyPanel.dragX) - 6,
                     y: Math.round(buddyPanel.dragY) - 16,
                     width: Math.round(buddyPanel.width) + 12,
                     height: Math.round(buddyPanel.height) + 26, radius: 22 })
        }
    }

    function updateMask() {
        const r = []
        if (!revealed) {
            r.push({ x: peekZone.x, y: 0, width: peekZone.width, height: peekZone.height, radius: 0 })
        } else {
            const m = 8
            let left = 1e9
            let right = -1e9
            if (musicShown) {
                left = musicDock.x - m
                right = musicDock.x + musicDock.width + m
            }
            if (timerPill.visible) {
                left = Math.min(left, timerPill.x - m)
                right = Math.max(right, timerPill.x + timerPill.width + m)
            }
            const tipActive = tipVisible || hudVisible || tip.opacity > 0.01
            const below = tipActive ? 52 : m
            if (right > left)
                r.push({ x: left, y: 0, width: right - left, height: shownY + pillH + below, radius: 26 })
            if (tipActive) {
                const tx0 = Math.round(Math.min(tip.x, tip.targetX) - m)
                const tx1 = Math.round(Math.max(tip.x + tip.width, tip.targetX + tip.targetWidth) + m)
                r.push({ x: tx0, y: shownY + pillH, width: Math.max(40, tx1 - tx0), height: 52, radius: 14 })
            }
            if (panelVisible) {
                r.push({ x: panelItem.x - m, y: panelItem.baseY - 8,
                         width: panelItem.width + 2 * m, height: panelTargetH + 2 * m + 8, radius: 24 })
            }
            if (hubVisible) {
                r.push({ x: hubFlyout.x - m, y: hubFlyout.baseY - 8,
                         width: hubFlyout.width + 2 * m, height: hubTargetH + 2 * m + 8, radius: 24 })
            }
            if (paletteVisible) {
                r.push({ x: paletteFlyout.x - m, y: paletteFlyout.baseY - 8,
                         width: paletteFlyout.width + 2 * m, height: paletteTargetH + 2 * m + 8, radius: 24 })
            }
        }
        pushBuddyRegions(r)
        Overlay.setInteractiveRegions(r)
    }

    // The JEV classifier's key: its own if set, otherwise the chat provider's key, but
    // only when both point at the same https host (so a key never goes to another host).
    function classifierKeyFor(ownKey, classifierBase, provider, chatBase, chatKey) {
        if (String(ownKey).length > 0)
            return String(ownKey)
        const host = (u) => {
            const m = /^https:\/\/([^\/:?#]+)/i.exec(String(u).trim())
            return m ? m[1].toLowerCase() : ""
        }
        const chatHost = host(chatBase) || (provider === "opencode" ? "opencode.ai" : "")
        const classHost = host(classifierBase)
        return classHost.length > 0 && classHost === chatHost ? String(chatKey) : ""
    }

    Component.onCompleted: {
        // Cloud TTS names such as en-US-Neural2-F cannot be used by Gemini TTS.
        // Move a saved setting to the Gemini default once, so Settings shows the
        // voice that will actually be used.
        if (cfg.buddyTtsGoogleVoice.length === 0
                || /^[a-z]{2,3}(-[A-Za-z]{2,4})?-/.test(cfg.buddyTtsGoogleVoice))
            cfg.buddyTtsGoogleVoice = "Kore"
        updateMask()
    }

    // ------------------------------------------------------------ peek dash
    // While tucked away, only this centered dash at the screen edge catches
    // the pointer (and file drags) to pull the notch out.
    PeekDash {
        id: peekZone
        root: root
    }

    function handleDrop(drop) {
        const result = Player.ingestDrop(drop.urls)
        const added = result.added || 0
        if (added > 0) {
            root.showToast(added === 1 ? qsTr("Added to player") : qsTr("Added %1 songs").arg(added))
        } else if ((result.folders || 0) === 0 && drop.hasUrls) {
            // Nothing anywhere knew how to use: say so instead of eating the drop.
            root.showToast(qsTr("Nothing here to add"))
        }
        drop.accept(Qt.CopyAction)
    }

    // ------------------------------------------------------------ music pill
    // Centred at the top edge: [♫] or [ |◀ ▶ ▶| ]. Buddy controls it too (the `music` tool).
    Item {
        id: musicDock
        width: nowPlaying.width
        height: root.pillH
        x: Math.round((root.width - width) / 2)
        y: root.hiddenY + (root.shownY - root.hiddenY) * root.progress
        opacity: root.clamp01(root.progress * 4) * root.musicFade
        visible: opacity > 0.01
        scale: 0.85 + 0.15 * root.clamp01(root.progress)
        transformOrigin: Item.Top

        HoverHandler { id: musicPillHover }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            target: null
            onWheel: (event) => root.setScale(cfg.sizeScale + (event.angleDelta.y / 120) * 0.05)
        }

        NowPlaying {
            id: nowPlaying
            s: root.s
            highlight: musicDrop.containsDrag
            appear: root.clamp01(root.progress * 1.4 - 0.2)
            onEmptyClicked: root.showToast(qsTr("Drop songs here, or ask Buddy to play some music"))
            // Right-click on the pill opens Settings only when Buddy is off; otherwise Buddy's own
            // right-click menu is the way in.
            onSettingsRequested: if (!cfg.buddyEnabled) root.toggleSettings()
            onVolumeNudged: (v) => root.showToast(qsTr("Volume %1%").arg(Math.round(v * 100)))
            onHintRequested: (text) => {
                root.hoverHint = text
                root.tipX = musicDock.mapToItem(root.contentItem, musicDock.width / 2, 0).x
                if (!root.tipReady)
                    tipTimer.restart()
            }
            onHintCleared: {
                root.hoverHint = ""
                tipTimer.stop()
                tipGrace.restart()
            }
        }

        DropArea {
            id: musicDrop
            anchors.fill: parent
            onDropped: (drop) => root.handleDrop(drop)
        }
    }

    // ------------------------------------------------------------ focus timer
    // The timer keeps running when its panel is closed; while a session is
    // on, a live pill beside the music pill shows the time left.
    FocusTimer {
        id: sessionTimer
        objectName: "sessionTimer"
        minutes: cfg.focusMinutes
        onMinutesChanged: if (cfg.focusMinutes !== minutes) cfg.focusMinutes = minutes
        onDone: {
            root.timerAlert = true
            root.showToast(qsTr("Focus session finished"))
            alertTimer.restart()
        }
    }
    property bool timerAlert: false
    Timer { id: alertTimer; interval: 12000; onTriggered: root.timerAlert = false }

    TimerPill {
        id: timerPill
        timer: sessionTimer
        s: root.s
        appear: root.clamp01(root.progress * 1.4 - 0.2)
        // Beside the music pill; in the middle of the top edge when there is none.
        x: root.musicShown ? musicDock.x + musicDock.width + root.musicGap : Math.round((root.width - width) / 2)
        y: musicDock.y
        transformOrigin: Item.Left

        onClicked: {
            root.timerAlert = false
            if (sessionTimer.finished)
                sessionTimer.reset()
            root.openHub(0)
        }
        onHintRequested: (text) => {
            root.hoverHint = text
            root.tipX = timerPill.mapToItem(root.contentItem, timerPill.width / 2, 0).x
            if (!root.tipReady)
                tipTimer.restart()
        }
        onHintCleared: {
            root.hoverHint = ""
            tipTimer.stop()
            tipGrace.restart()
        }
    }

    // ------------------------------------------------------------ resize grip
    ResizeGrip {
        id: grip
        root: root
        cfg: cfg
        pillItem: musicDock
        pillHovered: musicPillHover.hovered
    }

    // ------------------------------------------------------------ tooltip / toast / size HUD
    TooltipToast {
        id: tip
        root: root
        cfg: cfg
    }

    // ------------------------------------------------------------ settings panel
    SettingsFlyout {
        id: panelItem
        root: root
        cfg: cfg
    }

    // ------------------------------------------------------------ hub
    // Timer and Clipboard in one card, opened from the timer pill, the palette
    // ("Focus timer", "Clipboard history") or the buddy.
    HubFlyout {
        id: hubFlyout
        objectName: "hubFlyout"
        root: root
        sessionTimer: sessionTimer
    }

    // ------------------------------------------------------------ command palette
    // The hotkey's card: search apps, switch windows, do maths, search the
    // web, or run one of Noch's own commands.
    PaletteFlyout {
        id: paletteFlyout
        objectName: "paletteFlyout"
        root: root
    }

    // ------------------------------------------------------------ buddy
    // Noch's little helper: a creature hanging from a rope at the top edge.
    // Click it to ask for things. Real routes (focus timer, app opening,
    // palette, clipboard, mic) and clearly-labelled demo cards (chart,
    // research, plan) are implemented in Buddy.qml + BuddyRouter.qml; the
    // result card reuses the app's glass and motion conventions.
    readonly property real buddyAnchor: cfg.buddyX >= 0
        ? Math.max(36, Math.min(width - 36, cfg.buddyX))
        : Math.max(48, Math.min(Math.round(width * 0.14),
                                Math.round((width - pillW) / 2 - 64)))
    readonly property real buddyAnchorY: cfg.buddyY >= 0
        ? Math.max(8, Math.min(height - 64, cfg.buddyY))
        : root.shownY


    // Settings > Buddy > Ring colour: let the buddy show off the new halo.
    function previewBuddyRing() {
        if (!cfg.buddyEnabled)
            return
        buddy.setMood("happy", 2200)
        buddy.hop(90)
        buddy.neonBurst(true)
    }

    function previewBuddyFx(kind) {
        if (cfg.buddyEnabled)
            buddy.previewFx(kind)
    }

    Buddy {
        id: buddy
        visible: cfg.buddyEnabled
        solidRing: cfg.buddySolidRing
        ringStyle: cfg.buddyRingStyle
        ringThickness: cfg.buddyRingThickness
        ringSpeed: cfg.buddyRingSpeed
        glowStrength: cfg.buddyGlowStrength
        glowMode: cfg.buddyGlowMode
        glowAlways: cfg.buddyGlowAlways
        voiceFx: cfg.buddyVoiceFx
        motionPaused: root.fullscreenGuard || !visible
        s: root.s
        desktop: root.contentItem
        focusTimer: sessionTimer
        savedPlan: cfg.buddyPlan
        anchor: root.buddyAnchor
        anchorY: root.buddyAnchorY
        resultPanel: buddyPanel

        onBuddyMoved: (x, y) => {
            cfg.buddyX = Math.round(x)
            cfg.buddyY = Math.round(y)
        }
        onToastRequested: (text) => root.showToast(text)
        onPaletteRequested: root.summonPalette()
        onClipboardRequested: root.openHub(1)
        onSettingsRequested: root.toggleSettings()
        onActivationRequested: root.requestActivate()
        onPlanEdited: (plan) => cfg.buddyPlan = JSON.stringify(plan)
        onPermissionModeRequested: (mode) => cfg.buddyPermissionMode = mode
        voiceAutoSend: cfg.buddyVoiceAutoSend
        historyJson: cfg.buddyHistory
        onHistorySaved: (json) => cfg.buddyHistory = json
    }

    // Keep the C++ buddy provider and sound synthesizer in sync with saved settings.
    Binding { target: BuddyAgent; property: "provider"; value: cfg.buddyProvider }
    Binding { target: BuddyAgent; property: "apiKey"; value: cfg.buddyApiKey }
    Binding { target: BuddyAgent; property: "apiBase"; value: cfg.buddyApiBase }
    Binding { target: BuddyAgent; property: "model"; value: cfg.buddyModel }
    Binding { target: BuddyAgent; property: "temperature"; value: cfg.buddyTemperature }
    Binding { target: BuddyAgent; property: "agentMode"; value: cfg.buddyAgentMode }
    Binding { target: BuddyAgent; property: "allowBash"; value: cfg.buddyAllowBash }
    Binding { target: BuddyAgent; property: "allowComputer"; value: cfg.buddyAllowComputer }
    Binding { target: AgentControl; property: "showCursor"; value: cfg.buddyCuShowCursor }
    Binding { target: AgentControl; property: "showGlow"; value: cfg.buddyCuShowGlow }
    Binding { target: AgentControl; property: "glowIntensity"; value: cfg.buddyCuGlowIntensity }
    Binding { target: AgentControl; property: "glowStyle"; value: cfg.buddyCuGlowStyle }

    // Buddy's cursor, the screen-edge glow and the control HUD while the
    // agent uses the computer. The HUD sits just under the notch.
    AgentControlOverlay {
        id: agentOverlay
        hudScreen: root.screen
        hudCenterX: root.x + root.width / 2
        hudTop: root.y + root.shownY + root.pillH + 10
    }
    Binding { target: BuddyAgent; property: "allowEditor"; value: cfg.buddyAllowEditor }
    Binding { target: BuddyAgent; property: "allowCodeSearch"; value: cfg.buddyAllowCodeSearch }
    Binding { target: BuddyAgent; property: "allowMemory"; value: cfg.buddyAllowMemory }
    Binding { target: BuddyAgent; property: "allowWebSearch"; value: cfg.buddyAllowWebSearch }
    Binding { target: BuddyAgent; property: "allowToolSearch"; value: cfg.buddyAllowToolSearch }
    Binding { target: BuddyAgent; property: "allowAdvisor"; value: cfg.buddyAllowAdvisor }
    Binding { target: BuddyAgent; property: "allowTodoTask"; value: cfg.buddyAllowTodoTask }
    Binding { target: BuddyAgent; property: "toolDiet"; value: cfg.buddyToolDiet }
    Binding { target: BuddyAgent; property: "permissionMode"; value: cfg.buddyPermissionMode }
    Binding { target: BuddyAgent; property: "workspaceRoot"; value: cfg.buddyWorkspaceRoot }
    Binding { target: BuddyAgent; property: "redactSecrets"; value: cfg.buddyRedactSecrets }
    Binding { target: BuddyAgent; property: "keepTraces"; value: cfg.buddyKeepTraces }
    Binding { target: BuddyAgent; property: "streamReplies"; value: cfg.buddyStreamReplies }
    Binding { target: BuddyAgent; property: "showThinking"; value: cfg.buddyShowThinking }
    Binding { target: BuddyAgent; property: "handoffs"; value: cfg.buddyHandoffs }
    Binding { target: BuddyAgent; property: "classifierBase"; value: cfg.buddyClassifierBase }
    // Own key if set; otherwise the OpenCode chat key, but only for an OpenCode address,
    // so a key is never sent to a host it was not made for.
    Binding {
        target: BuddyAgent; property: "classifierKey"
        value: root.classifierKeyFor(cfg.buddyClassifierKey, cfg.buddyClassifierBase,
                                     cfg.buddyProvider, cfg.buddyApiBase, cfg.buddyApiKey)
    }
    Binding { target: BuddyAgent; property: "classifierModel"; value: cfg.buddyClassifierModel }
    Binding { target: BuddyAgent; property: "classifierEnabled"; value: cfg.buddyClassifierEnabled }
    Binding { target: BuddyAgent; property: "officialComputerTools"; value: cfg.buddyOfficialComputer }
    Binding { target: BuddyAgent; property: "computerDetail"; value: cfg.buddyComputerDetail }
    Binding { target: BuddyAgent; property: "maxSteps"; value: cfg.buddyMaxSteps }
    Binding { target: BuddyAgent; property: "allowAskUser"; value: cfg.buddyAllowAskUser }
    Binding { target: BuddyAgent; property: "allowScreenshots"; value: cfg.buddyAllowScreenshots }
    Binding { target: BuddyAgent; property: "allowMusic"; value: cfg.buddyAllowMusic }
    Binding { target: Voice; property: "enabled"; value: cfg.buddyVoiceEnabled }
    Binding { target: TalkKey; property: "enabled"; value: (cfg.buddyVoiceEnabled || cfg.buddyLiveEnabled) && cfg.buddyVoiceHotkey }
    Binding { target: Wake; property: "enabled"; value: cfg.buddyWakeEnabled }
    Binding { target: Wake; property: "phrase"; value: cfg.buddyWakePhrase }
    Binding { target: Wake; property: "threshold"; value: cfg.buddyWakeThreshold }
    // With a live conversation the wake phrase starts it, and needs no speech to text of its own.
    Binding { target: Wake; property: "handsOff"; value: cfg.buddyLiveEnabled }
    Binding { target: Speech; property: "enabled"; value: cfg.buddySpeakReports }
    Binding { target: Speech; property: "engine"; value: cfg.buddyTtsEngine }
    Binding { target: Speech; property: "googleKey"; value: cfg.buddyTtsGoogleKey }
    Binding { target: Speech; property: "googleVoice"; value: cfg.buddyTtsGoogleVoice }
    Binding { target: Speech; property: "kokoroBase"; value: cfg.buddyTtsKokoroBase }
    Binding { target: Speech; property: "kokoroVoice"; value: cfg.buddyTtsKokoroVoice }
    Binding { target: Speech; property: "speed"; value: cfg.buddyTtsSpeed }
    Binding { target: Speech; property: "speakAll"; value: cfg.buddyTtsSpeakAll }
    // Deepgram: one key for the conversation, the listener and the voice.
    Binding { target: Speech; property: "deepgramKey"; value: cfg.buddyDgKey }
    Binding { target: Speech; property: "deepgramVoice"; value: cfg.buddyLiveVoice }
    Binding { target: Voice; property: "deepgramKey"; value: cfg.buddyDgKey }
    Binding { target: Voice; property: "deepgramModel"; value: cfg.buddyLiveListenModel }
    Binding { target: Voice; property: "deepgramLanguage"; value: cfg.buddyLiveLanguage }
    Binding { target: Voice; property: "deepgramKeyterms"; value: cfg.buddyLiveKeyterms }
    Binding { target: Voice; property: "sttEngine"; value: cfg.buddySttEngine }
    Binding { target: Youtube; property: "configuredPath"; value: cfg.buddyYtDlpPath }
    Binding { target: Live; property: "enabled"; value: cfg.buddyLiveEnabled }
    Binding { target: Live; property: "apiKey"; value: cfg.buddyDgKey }
    Binding { target: Live; property: "listenModel"; value: cfg.buddyLiveListenModel }
    Binding { target: Live; property: "listenLanguage"; value: cfg.buddyLiveLanguage }
    Binding { target: Live; property: "keyterms"; value: cfg.buddyLiveKeyterms }
    Binding { target: Live; property: "eotThreshold"; value: cfg.buddyLiveEot }
    Binding { target: Live; property: "thinkProvider"; value: cfg.buddyLiveProvider }
    Binding { target: Live; property: "thinkModel"; value: cfg.buddyLiveModel }
    Binding { target: Live; property: "temperature"; value: cfg.buddyLiveTemperature }
    Binding { target: Live; property: "prompt"; value: cfg.buddyLivePrompt }
    Binding { target: Live; property: "endpointUrl"; value: cfg.buddyLiveEndpoint }
    Binding { target: Live; property: "endpointKey"; value: cfg.buddyLiveEndpointKey }
    Binding { target: Live; property: "askBuddy"; value: cfg.buddyLiveAskBuddy }
    Binding { target: Live; property: "speakModel"; value: cfg.buddyLiveVoice }
    Binding { target: Live; property: "speakSpeed"; value: cfg.buddyLiveSpeed }
    Binding { target: Live; property: "volume"; value: cfg.buddyLiveVolume }
    Binding { target: Live; property: "greeting"; value: cfg.buddyLiveGreeting }
    Binding { target: Live; property: "audioMode"; value: cfg.buddyLiveAudioMode }
    Binding { target: Live; property: "followUpSeconds"; value: cfg.buddyLiveFollowUp }
    Binding { target: BuddySound; property: "enabled"; value: cfg.buddySfxEnabled }
    // The wake word chimes the instant it hears its phrase: keep the device open for it.
    Binding { target: BuddySound; property: "keepOpen"; value: Wake.active }
    Binding { target: BuddySound; property: "volume"; value: cfg.buddySfxVolume }
    Binding { target: BuddySound; property: "style"; value: cfg.buddySfxStyle }

    BuddyPanel {
        id: buddyPanel
        objectName: "buddyPanel"
        desktop: root.contentItem

        onDismissed: {
            open = false
            buddy.panelClosed()
        }
        onNewRequestRequested: buddy.openInput(true)
        onFollowUpSubmitted: (text) => buddy.submit(text)
        onContinueRequested: buddy.continueComputer()
        onLoved: () => buddy.celebrate()
        onSpeakingTick: () => buddy.pulseSpeaking()
        onRetryRequested: {
            phase = "loading"
            buddy.retryTask()
        }
        onPlanEdited: (plan) => cfg.buddyPlan = JSON.stringify(plan)
        onCapability: (id) => buddy.runCapability(id)
    }

    Connections {
        target: cfg
        function onBuddyEnabledChanged() {
            if (!cfg.buddyEnabled) {
                buddy.dismiss()
                Qt.callLater(root.updateMask)
            }
        }
    }

    Connections {
        target: Player
        function onPlaybackError(message) { root.showToast(message) }
        function onFolderScanNotice(message) { root.showToast(message) }
    }

    // Ctrl+Alt+Space: the global summon. The switch in Settings enables or
    // disables the registration; if the combination is taken, registration
    // simply fails and everything else keeps working.
    Connections {
        target: Hotkey
        function onActivated() { root.summonPalette() }
        function onRegisteredChanged() { root.hotkeyNotice() }
    }
    Binding { target: Hotkey; property: "enabled"; value: cfg.hotkeyPalette }

    // Where the music pill (and the timer pill beside it) sits, so the watcher can tell
    // when an app window occupies that space. The zone is the shown position, whether or
    // not the pill is currently tucked away.
    Binding { target: Fullscreen; property: "window"; value: root }
    Connections {
        target: Fullscreen
        function onOccupiedChanged() {
            console.log("noch qml: top space", Fullscreen.occupied ? "taken by " + Fullscreen.occupiedBy : "clear")
        }
    }
    Binding {
        target: Fullscreen; property: "zone"
        value: Qt.rect(musicDock.x - 8, 0,
                       (timerPill.visible ? timerPill.x + timerPill.width - musicDock.x : musicDock.width) + 16,
                       root.shownY + root.pillH + 8)
    }

    Connections {
        target: AppIndex
        function onCommandRequested(command) { root.runCommand(command) }
        function onToastRequested(text) { root.showToast(text) }
    }

    Connections {
        target: Radio
        function onDenied() { root.showToast(qsTr("Windows blocked that switch")) }
    }

    Connections {
        target: Night
        function onFailed() { root.showToast(qsTr("Night light could not be switched here")) }
    }
}
