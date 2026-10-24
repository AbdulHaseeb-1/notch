pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
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
        // ---- Look & glow --------------------------------------------------------
        property string buddyRingStyle: "neon"
        property real buddyRingThickness: 1.0
        property real buddyRingSpeed: 1.0
        property bool buddyRingShader: true
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
        property bool buddyAllowTodoTask: true
        // Claude's own computer-use tools (no extra fee; they add about 4,500
        // input tokens per request, only while computer use is on).
        property bool buddyOfficialComputer: true
        // "sharp" sends bigger screenshots (more tokens); "standard" the smaller ones.
        property string buddyComputerDetail: "sharp"
        property int buddyMaxSteps: 40
        property bool buddyAllowAskUser: true
        property bool buddyAllowScreenshots: true
        property bool buddyAllowMusic: true
        // Connected apps (Settings > Connectors): their tools are offered to Buddy while this is on.
        property bool buddyAllowConnectors: true
        // Agent Skills: folders of instructions Buddy loads when a task matches. Names switched
        // off in Settings are kept comma separated.
        property bool buddyAllowSkills: true
        property string buddyDisabledSkills: ""
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
        // The wake phrases switched on, by model file name, comma separated
        // (the Settings page and Wake keep this in step both ways).
        property string buddyWakeWords: "hey_buddy.onnx"
        property bool buddySpeakReports: false
        // ---- Spoken answers: "google" | "deepgram" -------------------
        property string buddyTtsEngine: "google"
        property string buddyTtsGoogleKey: ""
        property string buddyTtsGoogleVoice: "Kore"
        property real buddyTtsSpeed: 1.0
        property bool buddyTtsSpeakAll: false
        // ---- Deepgram: one API key for the live conversation, speech to text and the voice ----
        property string buddyDgKey: ""
        // Speech to text for push-to-talk and the wake word is Deepgram (the
        // wake phrase itself is still found on this computer).
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
        property string buddyLiveGreeting: "I'm listening."
        property string buddyLiveAudioMode: "speakers"
        // Speaker mode: take Buddy's own voice out of the microphone (talk over it, keep the first words).
        property bool buddyLiveEchoCancel: true
        // Replies of a sentence or two; anything that needs thought goes to Buddy.
        property bool buddyLiveQuick: true
        // A soft sound while Buddy thinks, so a short pause does not feel like a hang.
        property bool buddyLiveThinkCue: true
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
        // ---- Long-term memory (Mem0) ------------------------------------------
        // "local" is the Docker stack in docker/mem0 (nothing leaves this computer),
        // "platform" is api.mem0.ai, "custom" is any address that serves the same API.
        property bool buddyMemEnabled: true
        property string buddyMemBackend: "local"
        property string buddyMemUrl: ""
        property string buddyMemKey: ""
        property string buddyMemUserId: "noch"
        // Local Mem0 borrows the chat provider; blank model = the chat model.
        property string buddyMemLlmModel: ""
        property string buddyMemEmbedModel: ""
        property string buddyMemEmbedBase: ""
        property string buddyMemEmbedKey: ""
        // ---- Desktop buddy sound effects -------------------------------------
        property bool buddySfxEnabled: true
        property real buddySfxVolume: 0.75
        property string buddySfxStyle: "warm"
    }

    readonly property string snapshotKey: [
        cfg.sizeScale, cfg.autoHide, cfg.hideDelay,
        cfg.focusMinutes, cfg.hotkeyPalette, cfg.suppressInFullscreen,
        cfg.buddyEnabled, Math.round(cfg.buddyX), Math.round(cfg.buddyY),
        cfg.buddyPlan.length, cfg.buddyHistory.length, cfg.buddyProvider,
        cfg.buddyApiBase, cfg.buddyModel, cfg.buddyTemperature,
        cfg.buddyAgentMode, cfg.buddyAllowBash, cfg.buddyAllowComputer, cfg.buddyCuShowCursor, cfg.buddyCuShowGlow, cfg.buddyCuGlowIntensity, cfg.buddyCuGlowStyle, cfg.buddyRingStyle, cfg.buddyRingThickness, cfg.buddyRingSpeed, cfg.buddyRingShader, cfg.buddyGlowStrength, cfg.buddyGlowMode, cfg.buddyGlowAlways, cfg.buddyVoiceFx, cfg.buddyAllowEditor,
        cfg.buddyAllowCodeSearch, cfg.buddyAllowMemory, cfg.buddyAllowWebSearch,
        cfg.buddyAllowTodoTask,
        cfg.buddyOfficialComputer, cfg.buddyComputerDetail, cfg.buddyMaxSteps, cfg.buddyAllowAskUser, cfg.buddyAllowScreenshots, cfg.buddyAllowMusic, cfg.buddyAllowConnectors, cfg.buddyAllowSkills, cfg.buddyDisabledSkills,
        cfg.buddyVoiceEnabled, cfg.buddyVoiceHotkey, cfg.buddyVoiceAutoSend,
        cfg.buddyWakeEnabled, cfg.buddyWakePhrase, cfg.buddyWakeThreshold, cfg.buddyWakeWords, cfg.buddySpeakReports,
        cfg.buddyLiveEnabled, cfg.buddyLiveListenModel, cfg.buddyLiveLanguage, cfg.buddyLiveProvider, cfg.buddyLiveModel,
        cfg.buddyLiveVoice, cfg.buddyLiveSpeed, cfg.buddyLiveVolume, cfg.buddyLiveAudioMode, cfg.buddyLiveFollowUp, cfg.buddyLiveAskBuddy, cfg.buddyLiveEchoCancel, cfg.buddyLiveQuick, cfg.buddyLiveThinkCue,
        cfg.buddyTtsEngine, cfg.buddyTtsGoogleKey, cfg.buddyTtsGoogleVoice, cfg.buddyTtsSpeed, cfg.buddyTtsSpeakAll,
        cfg.buddyPermissionMode, cfg.buddyWorkspaceRoot, cfg.buddyRedactSecrets, cfg.buddyKeepTraces, cfg.buddyStreamReplies, cfg.buddyShowThinking,
        cfg.buddyMemEnabled, cfg.buddyMemBackend, cfg.buddyMemUrl, cfg.buddyMemUserId, cfg.buddyMemLlmModel, cfg.buddyMemEmbedModel, cfg.buddyMemEmbedBase,
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
    // The pill opens into the player card while a track is loaded. An app window in the space
    // it would take tucks the card up under the top edge, leaving a sliver that a click pops out;
    // it goes back under once the pointer has left the popped card.
    readonly property bool musicExpanded: Media.hasTrack && musicShown && !fullscreenGuard
    readonly property bool musicWindowed: Media.hasTrack && Fullscreen.occupied && !fullscreenGuard
    property bool musicPopped: false
    readonly property bool musicSliver: musicWindowed && musicShown && !musicPopped
    readonly property real sliverH: Math.round(nowPlaying.cardH * 0.05)
    readonly property real tuckY: sliverH - nowPlaying.cardH
    Timer {
        id: popBackTimer
        interval: 900
        onTriggered: if (!musicPillHover.hovered) root.musicPopped = false
    }
    property real musicFade: musicShown ? 1 : 0
    Behavior on musicFade { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    // Something at the top edge worth revealing at all.
    readonly property bool notchHasContent: musicShown || timerPill.visible
    readonly property real shownY: topGap
    readonly property real hiddenY: -Math.max(pillH, nowPlaying.cardH) - 4

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
                                     || dragging || timerAlert || musicPopped
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

    // Where the notch sits for `progress` alone, and the music card's tuck (1 = only its sliver
    // shows, under the top edge). The tuck has its own animation so it slides in and out smoothly.
    readonly property real dockRowY: hiddenY + (shownY - hiddenY) * progress
    property real tuck: musicSliver ? 1 : 0
    Behavior on tuck { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }

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
    // The buddy has a window of its own, so focus moving between the two windows is not
    // "clicking elsewhere": the check waits a moment and only acts when neither has it.
    function closeOnFocusLoss() {
        if (root.active || buddyWin.active)
            return
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
    Timer { id: focusLossTimer; interval: 80; onTriggered: root.closeOnFocusLoss() }
    onActiveChanged: if (!active) focusLossTimer.restart()

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
    readonly property bool buddyInteracting: buddyPanel.interacting
    readonly property string maskKey: [revealed, fullyHidden, musicSliver, musicPopped, Math.round(musicDock.height), width,
                                    Math.round(musicDock.x), Math.round(musicDock.width), Math.round(timerPill.width),
                                    musicShown, Math.round(timerPill.x),
                                    panelVisible, Math.round(panelTargetH),
                                    hubVisible, Math.round(hubTargetH),
                                    paletteVisible, Math.round(paletteFlyout.width),
                                    Math.round(paletteTargetH),
                                    tipVisible, hudVisible, tip.opacity > 0.01,
                                    Math.round(tip.targetX), Math.round(tip.targetWidth),
                                    cfg.buddyEnabled, buddyInteracting,
                                    buddyPanel.open, buddyPanel.visible,
                                    buddyInteracting ? 0 : Math.round(buddyPanel.dragX),
                                    buddyInteracting ? 0 : Math.round(buddyPanel.dragY),
                                    Math.round(buddyPanel.width),
                                    buddyInteracting ? 0 : Math.round(buddyPanel.height)].join(",")
    onMaskKeyChanged: Qt.callLater(updateMask)

    // The buddy's own regions belong to its window (see syncBuddyWindow); this window only
    // carries the result panel. While the panel is dragged, expand to the full window rect so
    // Win32 SetWindowRgn is called only once at drag start and once on release.
    function pushBuddyRegions(r) {
        if (!cfg.buddyEnabled || !buddy.visible)
            return
        if (buddyInteracting) {
            r.push({ x: 0, y: 0, width: root.width, height: root.height, radius: 0 })
            return
        }
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
                r.push({ x: left, y: 0, width: right - left, height: musicSliver ? sliverH + 4 : shownY + musicDock.height + below, radius: 26 })
            if (tipActive) {
                const tx0 = Math.round(Math.min(tip.x, tip.targetX) - m)
                const tx1 = Math.round(Math.max(tip.x + tip.width, tip.targetX + tip.targetWidth) + m)
                r.push({ x: tx0, y: shownY + musicDock.height, width: Math.max(40, tx1 - tx0), height: 52, radius: 14 })
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
        if (musicSliver)
            r.push({ x: musicDock.x - 8, y: 0, width: musicDock.width + 16, height: sliverH + 4, radius: 12 })
        pushBuddyRegions(r)
        Overlay.setInteractiveRegions(r)
    }

    Component.onCompleted: {
        // Cloud TTS names such as en-US-Neural2-F cannot be used by Gemini TTS.
        // Move a saved setting to the Gemini default once, so Settings shows the
        // voice that will actually be used.
        if (cfg.buddyTtsGoogleVoice.length === 0
                || /^[a-z]{2,3}(-[A-Za-z]{2,4})?-/.test(cfg.buddyTtsGoogleVoice))
            cfg.buddyTtsGoogleVoice = "Kore"
        updateMask()
        Qt.callLater(syncBuddyWindow)
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
        height: nowPlaying.height
        x: Math.round((root.width - width) / 2)
        y: root.dockRowY + (root.tuckY - root.dockRowY) * root.tuck
        opacity: root.clamp01(Math.max(root.progress, root.tuck) * 4) * root.musicFade
        visible: opacity > 0.01
        scale: 0.85 + 0.15 * root.clamp01(Math.max(root.progress, root.tuck))
        transformOrigin: Item.Top

        HoverHandler {
            id: musicPillHover
            onHoveredChanged: {
                if (hovered)
                    popBackTimer.stop()
                else if (root.musicPopped)
                    popBackTimer.restart()
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            target: null
            onWheel: (event) => root.setScale(cfg.sizeScale + (event.angleDelta.y / 120) * 0.05)
        }

        NowPlaying {
            id: nowPlaying
            s: root.s
            expanded: root.musicExpanded
            tucked: root.musicSliver
            onPopRequested: root.musicPopped = true
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

    // The buddy lives in a window of its own, just big enough for the orb, its light, its
    // bubble and its menus. Carrying it then moves that window natively: the window system
    // shifts the finished picture, and nothing here is drawn again (before, every step of a
    // drag redrew the whole screen-sized window, settings page and all).
    //
    // Inside the window the buddy still works in desktop coordinates (the same ones as this
    // window's): `world` is a screen-sized item shifted by -origin, so the window shows the
    // part of the desktop it sits over, and the buddy, its bubble and the result panel keep
    // using the numbers they always did.
    Window {
        id: buddyWin
        title: qsTr("Noch Buddy")
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint
        visible: cfg.buddyEnabled && geometryReady
        // Where the window's top-left corner is, in desktop coordinates.
        property real originX: 0
        property real originY: 0
        property bool geometryReady: false

        Shortcut { sequence: "Esc"; onActivated: root.closeAll() }
        Shortcut { sequence: "Ctrl+,"; onActivated: root.toggleSettings() }
        onActiveChanged: if (!active) focusLossTimer.restart()

        Item {
            id: world
            x: -buddyWin.originX
            y: -buddyWin.originY
            width: root.width
            height: root.height

            Buddy {
                id: buddy
                visible: cfg.buddyEnabled
                nativeDrag: true
                ringStyle: cfg.buddyRingStyle
                ringThickness: cfg.buddyRingThickness
                ringSpeed: cfg.buddyRingSpeed
                ringShader: cfg.buddyRingShader
                glowStrength: cfg.buddyGlowStrength
                liveThinkCue: cfg.buddyLiveThinkCue
                glowMode: cfg.buddyGlowMode
                glowAlways: cfg.buddyGlowAlways
                voiceFx: cfg.buddyVoiceFx
                motionPaused: root.fullscreenGuard || !visible
                s: root.s
                desktop: world
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
                // The bubble's text field is in this window, so this is the one that takes the keyboard.
                onActivationRequested: buddyWin.requestActivate()
                onPlanEdited: (plan) => cfg.buddyPlan = JSON.stringify(plan)
                onPermissionModeRequested: (mode) => cfg.buddyPermissionMode = mode
                voiceAutoSend: cfg.buddyVoiceAutoSend
                historyJson: cfg.buddyHistory
                onHistorySaved: (json) => cfg.buddyHistory = json
                onDragMoved: root.carryBuddyWindow()
            }
        }
    }

    // Fits the buddy's window round what the buddy shows, and gives it the click-through
    // mask the buddy already works out (the orb and its light, the bubble, the menu, the chips).
    //
    // The window must NOT follow every change of those regions. Resizing or shifting a window is
    // not free of tearing: for a frame or two the old picture sits at the new place, so a window
    // that tracked a streaming answer line by line made the orb and the bubble jump a few pixels
    // all the time. So the window is sized for the largest bubble from the start (opening it
    // changes nothing), only grows, a chunk at a time, when something else (a menu, a chip) needs
    // more, and only ever shrinks at its right and bottom edges (moving the left or top edge
    // shifts the picture) when things have been quiet for a while. The rectangle is kept relative
    // to the orb's centre, so it travels with the orb: after a drag the new place needs no resize.
    //
    // mode: "fit" (the buddy was moved or resized: fit afresh), "grow" (default), "shrink".
    property var buddyRel: null            // { x0, y0, x1, y1 } relative to the orb's centre
    function syncBuddyWindow(mode) {
        if (!cfg.buddyEnabled || buddy.dragging)
            return
        const rs = buddy.regionsIn(world)
        if (rs.length === 0)
            return
        const cx = Math.floor(buddy.x + buddy.width / 2)
        const cy = Math.floor(buddy.y + buddy.height / 2)
        const rest = Math.ceil(buddy.orbRadius + 40 * buddy.s)
        const need = { x0: -rest, y0: -rest, x1: rest, y1: rest }
        const take = (x0, y0, x1, y1) => {
            need.x0 = Math.min(need.x0, x0 - cx)
            need.y0 = Math.min(need.y0, y0 - cy)
            need.x1 = Math.max(need.x1, x1 - cx)
            need.y1 = Math.max(need.y1, y1 - cy)
        }
        for (let i = 0; i < rs.length; ++i)
            take(rs[i].x, rs[i].y, rs[i].x + rs[i].width, rs[i].y + rs[i].height)
        // Always room for the largest bubble, open or not: opening it then changes nothing about
        // the window (a window that grew when the bubble opened showed a ghost of the orb at the
        // wrong place for a frame or two). It is transparent and click-through where nothing is.
        const br = buddy.bubbleReach()
        if (br)
            take(br.x, br.y, br.x + br.width, br.y + br.height)

        const slack = 48
        const padded = { x0: need.x0 - slack, y0: need.y0 - slack, x1: need.x1 + slack, y1: need.y1 + slack }
        const cur = root.buddyRel
        let next = cur
        if (!cur || mode === "fit") {
            next = padded
        } else if (need.x0 < cur.x0 || need.y0 < cur.y0 || need.x1 > cur.x1 || need.y1 > cur.y1) {
            next = { x0: Math.min(cur.x0, padded.x0), y0: Math.min(cur.y0, padded.y0),
                     x1: Math.max(cur.x1, padded.x1), y1: Math.max(cur.y1, padded.y1) }
        } else if (mode === "shrink") {
            next = { x0: cur.x0, y0: cur.y0,
                     x1: cur.x1 > padded.x1 + 40 ? padded.x1 : cur.x1,
                     y1: cur.y1 > padded.y1 + 40 ? padded.y1 : cur.y1 }
        }
        if (!cur || next.x0 !== cur.x0 || next.y0 !== cur.y0 || next.x1 !== cur.x1 || next.y1 !== cur.y1)
            root.buddyRel = next
        else
            next = cur
        if (mode !== "shrink")
            buddyQuiet.restart()

        const ox = cx + next.x0
        const oy = cy + next.y0
        const local = []
        for (let i = 0; i < rs.length; ++i)
            local.push({ x: rs[i].x - ox, y: rs[i].y - oy, width: rs[i].width, height: rs[i].height, radius: rs[i].radius })
        buddyWin.originX = ox
        buddyWin.originY = oy
        Overlay.setWindowGeometry(buddyWin, root.x + ox, root.y + oy, next.x1 - next.x0, next.y1 - next.y0)
        Overlay.setWindowMask(buddyWin, local)
        buddyWin.geometryReady = true
    }
    // Quiet for a while: see whether the window can shrink back.
    Timer { id: buddyQuiet; interval: 2500; onTriggered: root.syncBuddyWindow("shrink") }

    // While the buddy is carried: move the window, nothing else.
    function carryBuddyWindow() {
        Overlay.setWindowPosition(buddyWin,
                                  root.x + buddyWin.originX + Math.round(buddy.dragOffsetX),
                                  root.y + buddyWin.originY + Math.round(buddy.dragOffsetY))
    }

    Connections {
        target: buddy
        function onMaskSigChanged() { Qt.callLater(root.syncBuddyWindow) }
        function onXChanged() { root.syncBuddyWindow("fit") }
        function onYChanged() { root.syncBuddyWindow("fit") }
        function onWidthChanged() { Qt.callLater(root.syncBuddyWindow, "fit") }
        function onVisibleChanged() { Qt.callLater(root.syncBuddyWindow) }
        // Put down (or cancelled): the window settles where the buddy now is.
        function onDraggingChanged() { Qt.callLater(root.syncBuddyWindow, "fit") }
    }
    onWidthChanged: Qt.callLater(syncBuddyWindow)
    onHeightChanged: Qt.callLater(syncBuddyWindow)

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
        hudTop: root.y + root.shownY + musicDock.height + 10
    }
    Binding { target: BuddyAgent; property: "allowEditor"; value: cfg.buddyAllowEditor }
    Binding { target: BuddyAgent; property: "allowCodeSearch"; value: cfg.buddyAllowCodeSearch }
    Binding { target: BuddyAgent; property: "allowMemory"; value: cfg.buddyAllowMemory }
    Binding { target: BuddyAgent; property: "allowWebSearch"; value: cfg.buddyAllowWebSearch }
    Binding { target: BuddyAgent; property: "allowTodoTask"; value: cfg.buddyAllowTodoTask }
    Binding { target: BuddyAgent; property: "permissionMode"; value: cfg.buddyPermissionMode }
    Binding { target: BuddyAgent; property: "workspaceRoot"; value: cfg.buddyWorkspaceRoot }
    Binding { target: BuddyAgent; property: "redactSecrets"; value: cfg.buddyRedactSecrets }
    Binding { target: BuddyAgent; property: "keepTraces"; value: cfg.buddyKeepTraces }
    Binding { target: BuddyAgent; property: "streamReplies"; value: cfg.buddyStreamReplies }
    Binding { target: BuddyAgent; property: "showThinking"; value: cfg.buddyShowThinking }
    Binding { target: BuddyAgent; property: "memBackend"; value: cfg.buddyMemBackend }
    Binding { target: BuddyAgent; property: "memUrl"; value: cfg.buddyMemUrl }
    Binding { target: BuddyAgent; property: "memKey"; value: cfg.buddyMemKey }
    Binding { target: BuddyAgent; property: "memUserId"; value: cfg.buddyMemUserId }
    Binding { target: BuddyAgent; property: "memLlmModel"; value: cfg.buddyMemLlmModel }
    Binding { target: BuddyAgent; property: "memEmbedModel"; value: cfg.buddyMemEmbedModel }
    Binding { target: BuddyAgent; property: "memEmbedBase"; value: cfg.buddyMemEmbedBase }
    Binding { target: BuddyAgent; property: "memEmbedKey"; value: cfg.buddyMemEmbedKey }
    Binding { target: BuddyAgent; property: "memEnabled"; value: cfg.buddyMemEnabled }
    Binding { target: BuddyAgent; property: "officialComputerTools"; value: cfg.buddyOfficialComputer }
    Binding { target: BuddyAgent; property: "computerDetail"; value: cfg.buddyComputerDetail }
    Binding { target: BuddyAgent; property: "maxSteps"; value: cfg.buddyMaxSteps }
    Binding { target: BuddyAgent; property: "allowAskUser"; value: cfg.buddyAllowAskUser }
    Binding { target: BuddyAgent; property: "allowScreenshots"; value: cfg.buddyAllowScreenshots }
    Binding { target: BuddyAgent; property: "allowMusic"; value: cfg.buddyAllowMusic }
    Binding { target: BuddyAgent; property: "allowConnectors"; value: cfg.buddyAllowConnectors }
    Binding { target: BuddyAgent; property: "allowSkills"; value: cfg.buddyAllowSkills }
    Binding { target: BuddyAgent; property: "disabledSkills"; value: cfg.buddyDisabledSkills }
    Binding { target: Voice; property: "enabled"; value: cfg.buddyVoiceEnabled }
    Binding { target: TalkKey; property: "enabled"; value: (cfg.buddyVoiceEnabled || cfg.buddyLiveEnabled) && cfg.buddyVoiceHotkey }
    Binding { target: Wake; property: "enabled"; value: cfg.buddyWakeEnabled }
    Binding { target: Wake; property: "phrase"; value: cfg.buddyWakePhrase }
    Binding { target: Wake; property: "threshold"; value: cfg.buddyWakeThreshold }
    Binding { target: Wake; property: "wakeWords"; value: cfg.buddyWakeWords.split(",").filter((s) => s.trim().length > 0) }
    // Adding a model in Settings switches it on inside Wake; write that back
    // so the choice survives a restart. (The Binding above pushes the saved
    // set down; this is the only way it comes back up.)
    Connections {
        target: Wake
        function onWakeWordsChanged() {
            const v = Wake.wakeWords.join(",")
            if (cfg.buddyWakeWords !== v)
                cfg.buddyWakeWords = v
        }
    }
    // With a live conversation the wake phrase starts it, and needs no speech to text of its own.
    Binding { target: Wake; property: "handsOff"; value: cfg.buddyLiveEnabled }
    Binding { target: Speech; property: "enabled"; value: cfg.buddySpeakReports }
    Binding { target: Speech; property: "engine"; value: cfg.buddyTtsEngine }
    Binding { target: Speech; property: "googleKey"; value: cfg.buddyTtsGoogleKey }
    Binding { target: Speech; property: "googleVoice"; value: cfg.buddyTtsGoogleVoice }
    Binding { target: Speech; property: "speed"; value: cfg.buddyTtsSpeed }
    Binding { target: Speech; property: "speakAll"; value: cfg.buddyTtsSpeakAll }
    // Deepgram: one key for the conversation, the listener and the voice.
    Binding { target: Speech; property: "deepgramKey"; value: cfg.buddyDgKey }
    Binding { target: Speech; property: "deepgramVoice"; value: cfg.buddyLiveVoice }
    Binding { target: Voice; property: "deepgramKey"; value: cfg.buddyDgKey }
    Binding { target: Voice; property: "deepgramModel"; value: cfg.buddyLiveListenModel }
    Binding { target: Voice; property: "deepgramLanguage"; value: cfg.buddyLiveLanguage }
    Binding { target: Voice; property: "deepgramKeyterms"; value: cfg.buddyLiveKeyterms }
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
    Binding { target: Live; property: "echoCancel"; value: cfg.buddyLiveEchoCancel }
    Binding { target: Live; property: "quickReplies"; value: cfg.buddyLiveQuick }
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
    // The zone is the pill's footprint once it is open (the card, while a track is loaded), so it
    // does not move as the pill opens or closes: an app that gets under the card then closes it.
    function musicZone() {
        const cx = root.width / 2
        const half = nowPlaying.footW / 2
        const right = timerPill.visible ? cx + half + musicGap + timerPill.width : cx + half
        return Qt.rect(cx - half - 8, 0, right - (cx - half) + 16, shownY + nowPlaying.footH + 8)
    }
    Binding {
        target: Fullscreen; property: "zone"
        value: root.musicZone()
    }

    Connections {
        target: AppIndex
        function onCommandRequested(command) { root.runCommand(command) }
        function onToastRequested(text) { root.showToast(text) }
    }
}
