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
    readonly property var addAppDialog: fileDialog
    readonly property var addMusicDialog: musicDialog
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
        property bool monoIcons: true
        property bool showClock: true
        property int focusMinutes: 25
        property bool hotkeyPalette: true
        property bool suppressInFullscreen: true
        property bool buddyEnabled: true
        property bool buddySolidRing: true
        property real buddyX: -1
        property real buddyY: -1
        property string buddyPlan: ""
        // Past questions and answers, newest first (JSON, capped in Buddy.qml).
        property string buddyHistory: "[]"
        // ---- Desktop buddy AI provider ---------------------------------------
        // "none" = demo fixtures only. "openai"/"custom" = OpenAI-compatible
        // chat/completions endpoint. "ollama" = local Ollama. "anthropic" =
        // Anthropic Messages API.
        property string buddyProvider: "none"
        property string buddyApiKey: ""
        property string buddyApiBase: ""
        property string buddyModel: "gpt-4o-mini"
        property real buddyTemperature: 0.7
        // ---- Anthropic Agent SDK & Computer Use tools ------------------------
        property bool buddyAgentMode: true
        property bool buddyAllowBash: true
        // Computer use (mouse/keyboard control) is off until the user opts in.
        property bool buddyAllowComputer: false
        // How computer use looks while Buddy is in control.
        property bool buddyCuShowCursor: true
        property bool buddyCuShowGlow: true
        property real buddyCuGlowIntensity: 0.6
        property bool buddyAllowEditor: true
        property bool buddyAllowCodeSearch: true
        property bool buddyAllowMemory: true
        property bool buddyAllowWebSearch: true
        property bool buddyAllowToolSearch: true
        property bool buddyAllowAdvisor: true
        property bool buddyAllowTodoTask: true
        // Claude's own computer-use tools (no extra fee; they add about 4,500
        // input tokens per request, only while computer use is on).
        property bool buddyOfficialComputer: true
        // "sharp" sends bigger screenshots (more tokens); "standard" the smaller ones.
        property string buddyComputerDetail: "sharp"
        property int buddyMaxSteps: 40
        property bool buddyAllowAskUser: true
        // ---- Push-to-talk voice (off until the user adds a speech service) --
        property bool buddyVoiceEnabled: false
        property bool buddyVoiceHotkey: true
        property string buddyVoiceBase: "https://api.openai.com/v1"
        property string buddyVoiceKey: ""
        property string buddyVoiceModel: "whisper-1"
        property string buddyVoiceLanguage: ""
        property bool buddyVoiceAutoSend: false
        // ---- Agent safety (see docs/agent-safety.md) -------------------------
        // "default" asks before side effects, "acceptEdits" auto-approves file
        // edits inside the workspace, "plan" is read-only, "bypass" trusts all
        // (hard-deny rules still apply).
        property string buddyPermissionMode: "default"
        property string buddyWorkspaceRoot: ""
        property bool buddyRedactSecrets: true
        property bool buddyKeepTraces: true
        property bool buddyStreamReplies: true
        property bool buddyHandoffs: true
        // ---- Desktop buddy sound effects -------------------------------------
        property bool buddySfxEnabled: true
        property real buddySfxVolume: 0.75
        property string buddySfxStyle: "warm"
    }

    readonly property string snapshotKey: [
        cfg.sizeScale, cfg.autoHide, cfg.hideDelay, cfg.monoIcons, cfg.showClock,
        cfg.focusMinutes, cfg.hotkeyPalette, cfg.suppressInFullscreen,
        cfg.buddyEnabled, cfg.buddySolidRing, Math.round(cfg.buddyX), Math.round(cfg.buddyY),
        cfg.buddyPlan.length, cfg.buddyHistory.length, cfg.buddyProvider,
        cfg.buddyApiBase, cfg.buddyModel, cfg.buddyTemperature,
        cfg.buddyAgentMode, cfg.buddyAllowBash, cfg.buddyAllowComputer, cfg.buddyCuShowCursor, cfg.buddyCuShowGlow, cfg.buddyCuGlowIntensity, cfg.buddyAllowEditor,
        cfg.buddyAllowCodeSearch, cfg.buddyAllowMemory, cfg.buddyAllowWebSearch,
        cfg.buddyAllowToolSearch, cfg.buddyAllowAdvisor, cfg.buddyAllowTodoTask,
        cfg.buddyOfficialComputer, cfg.buddyComputerDetail, cfg.buddyMaxSteps, cfg.buddyAllowAskUser,
        cfg.buddyVoiceEnabled, cfg.buddyVoiceHotkey, cfg.buddyVoiceBase, cfg.buddyVoiceModel, cfg.buddyVoiceLanguage, cfg.buddyVoiceAutoSend,
        cfg.buddyPermissionMode, cfg.buddyWorkspaceRoot, cfg.buddyRedactSecrets, cfg.buddyKeepTraces, cfg.buddyStreamReplies, cfg.buddyHandoffs,
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
    readonly property real iconSize: Math.round(24 * s)
    readonly property real cellW: Math.round(44 * s)
    readonly property real padX: Math.round(12 * s)
    readonly property real musicGap: Math.round(8 * s)
    readonly property real topGap: 8
    readonly property real dashW: Math.round(44 * s)
    readonly property real dashH: Math.max(4, Math.round(4 * s))
    readonly property real pillW: Math.max(Math.round(150 * s), dockRow.implicitWidth + 2 * padX)
    property real animatedPillW: pillW
    Behavior on animatedPillW {
        enabled: !root.resizing
        NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
    }
    readonly property real shownY: topGap
    readonly property real hiddenY: -pillH - 4

    function setScale(v) {
        cfg.sizeScale = Math.round(Math.max(0.7, Math.min(1.6, v)) * 100) / 100
        hudTimer.restart()
    }

    // ---------------------------------------------------------- reveal logic
    property bool panelOpen: false
    property bool musicOpen: false
    property bool hubOpen: false
    property bool paletteOpen: false
    property int hubTab: 0
    property int menuIndex: -1
    property bool resizing: false
    property bool launchHold: false
    property bool introHold: true
    property bool revealed: true

    // While a full-screen app (a game, a video, a presentation) owns the
    // screen, hover is ignored so the notch never drops in over it. The hotkey
    // still works: opening the palette makes Noch the foreground window, which
    // lifts the guard by itself.
    // When tucked away (!revealed), ONLY hovering the dash (peekZone) brings
    // the notch out — hovering around it does nothing.
    readonly property bool fullscreenGuard: cfg.suppressInFullscreen && Fullscreen.active && !paletteOpen
    readonly property bool notchElementsHovered: pillHover.hovered || grip.containsMouse || panelItem.hovered
                                                 || menu.hovered || musicFlyout.hovered
                                                 || hubFlyout.hovered || musicPillHover.hovered
                                                 || previewCard.hovered || timerPill.hovered
                                                 || paletteFlyout.hovered
    readonly property bool pointerInside: !fullscreenGuard
                                          && (peekZone.containsMouse || (revealed && notchElementsHovered))
    readonly property bool dragging: peekZone.containsDrag || pillDrop.containsDrag || musicDrop.containsDrag
                                     || musicDrop.containsDrag
    readonly property bool holdOpen: !cfg.autoHide || panelOpen || musicOpen || hubOpen || paletteOpen
                                     || menuIndex >= 0 || resizing || launchHold || introHold
                                     || dragging || fileDialog.visible || musicDialog.visible || timerAlert
    readonly property bool wantVisible: pointerInside || holdOpen

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
    Timer { id: launchHoldTimer; interval: 1400; onTriggered: root.launchHold = false }

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
    function itemAppear(i) {
        const delay = Math.min(0.24, i * 0.024)
        return clamp01((progress - 0.15 - delay) / 0.55)
    }

    // ------------------------------------------------------------ tooltip
    property int hoverIndex: -1
    property real tipX: 0
    property bool tipReady: false
    property string toast: ""
    property string hoverHint: ""
    readonly property string tipText: toast.length > 0 ? toast
        : (hoverIndex >= 0 && hoverIndex < apps.length ? apps[hoverIndex].name : hoverHint)
    readonly property bool tipVisible: revealed && !resizing && !panelOpen && !musicOpen
                                       && !hubOpen && !paletteOpen && menuIndex < 0
                                       && tipText.length > 0
                                       && (toast.length > 0 || (tipReady && !previewShown))

    Timer { id: tipTimer; interval: 380; onTriggered: root.tipReady = true }
    Timer { id: tipGrace; interval: 160; onTriggered: if (root.hoverIndex < 0) root.tipReady = false }
    Timer { id: toastTimer; interval: 1300; onTriggered: root.toast = "" }
    Timer { id: hudTimer; interval: 900 }
    readonly property bool hudVisible: resizing || hudTimer.running

    // ------------------------------------------------------------ data
    readonly property var apps: Dock.apps
    readonly property var running: Dock.running
    readonly property var openList: OpenApps.apps

    function activateOrLaunchApp(i) {
        if (i < 0 || i >= apps.length)
            return
        previewOpen = false
        const app = apps[i]
        if (app.exe && running.indexOf(app.exe) >= 0 && OpenApps.activateApp(app.exe)) {
            closeAll()
            return
        }
        launchApp(i)
    }

    function launchApp(i) {
        if (i < 0 || i >= apps.length)
            return
        previewOpen = false
        menuIndex = -1
        panelOpen = false
        musicOpen = false
        hubOpen = false
        paletteOpen = false
        launchHold = true
        launchHoldTimer.restart()
        if (Dock.launch(i))
            toast = qsTr("Opening %1").arg(apps[i].name)
        else
            toast = qsTr("Couldn't open %1").arg(apps[i].name)
        toastTimer.restart()
    }

    function openMenu(i, centerX) {
        panelOpen = false
        musicOpen = false
        hubOpen = false
        paletteOpen = false
        menuX = centerX
        menuIndex = (menuIndex === i) ? -1 : i
    }

    // Closing the app's own surfaces. This is what empty-notch clicks, Esc and
    // launching an app reach, so it tucks the buddy away instead of dismissing
    // it: a reply still being made keeps running and pops back when ready.
    function closeAll() {
        panelOpen = false
        musicOpen = false
        hubOpen = false
        paletteOpen = false
        menuIndex = -1
        buddy.stash()
    }

    function toggleMusic() {
        menuIndex = -1
        panelOpen = false
        hubOpen = false
        paletteOpen = false
        musicOpen = !musicOpen
    }

    // Opens the hub on a page (or closes it if that page is already showing).
    // tab -1 means "wherever it was last".
    function toggleHub(tab) {
        menuIndex = -1
        panelOpen = false
        musicOpen = false
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
        menuIndex = -1
        panelOpen = false
        musicOpen = false
        paletteOpen = false
        if (!revealed)
            revealed = true
        hubTab = tab
        hubOpen = true
    }

    function toggleSettings() {
        menuIndex = -1
        musicOpen = false
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
        menuIndex = -1
        panelOpen = false
        musicOpen = false
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
    onActiveChanged: if (!active && !fileDialog.visible && !musicDialog.visible) {
        panelOpen = false
        musicOpen = false
        hubOpen = false
        paletteOpen = false
        menuIndex = -1
        buddy.stash()
    }

    Shortcut { sequence: "Esc"; onActivated: root.closeAll() }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.toggleSettings() }

    // ------------------------------------------------------------ mask
    // Only the visible parts of the window take the mouse; everything else
    // clicks through to the desktop.
    property real menuX: 0
    readonly property bool panelVisible: panelItem.shown
    readonly property bool menuVisible: menu.shown
    readonly property bool musicVisible: musicFlyout.shown
    readonly property bool hubVisible: hubFlyout.shown
    readonly property bool paletteVisible: paletteFlyout.shown
    // Flyout sizes for the mask are the sizes they are heading for, not the
    // animated ones, so the mask is not rebuilt on every frame of an animation.
    readonly property real panelTargetH: panelItem.targetHeight
    readonly property real hubTargetH: hubFlyout.targetHeight
    readonly property real musicTargetH: musicFlyout.targetHeight
    readonly property real paletteTargetH: paletteFlyout.targetHeight
    // The mask key is a STRING, not an array: Qt compares property values with
    // strict equality, and a fresh JS array never equals the previous one, so
    // every single dependency change (including no-op hover ticks) re-fired
    // onMaskKeyChanged and queued a full Win32 region rebuild. Joined, the
    // comparison actually succeeds and identical states cost nothing.
    readonly property bool buddyInteracting: buddy.dragging || buddyPanel.interacting
    readonly property string maskKey: [revealed, fullyHidden, Math.round(pillW), Math.round(pillH), width,
                                    Math.round(musicDock.width), Math.round(timerPill.width),
                                    panelVisible, Math.round(panelTargetH), menuVisible,
                                    Math.round(menu.x), Math.round(menu.height),
                                    musicVisible, Math.round(musicFlyout.x), Math.round(musicTargetH),
                                    hubVisible, Math.round(hubTargetH),
                                    paletteVisible, Math.round(paletteFlyout.width),
                                    Math.round(paletteTargetH),
                                    tipVisible, hudVisible, tip.opacity > 0.01,
                                    Math.round(tip.targetX), Math.round(tip.targetWidth),
                                    previewShown, Math.round(previewCard.x),
                                    Math.round(previewCard.width), Math.round(previewCard.height),
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
            const effPillW = Math.max(pillW, animatedPillW)
            const left = (width - effPillW) / 2 - m
            const rightEdge = timerPill.visible ? timerPill.x + timerPill.width
                                                : (width + effPillW) / 2 + musicGap + musicDock.width
            const right = Math.max(rightEdge, (width + effPillW) / 2 + musicGap + musicDock.width) + m
            const tipActive = tipVisible || hudVisible || tip.opacity > 0.01
            const below = tipActive ? 52 : m
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
            if (menuVisible) {
                r.push({ x: menu.x - m, y: menu.baseY - 8,
                         width: menu.width + 2 * m, height: menu.height + 2 * m + 8, radius: 20 })
            }
            if (musicVisible) {
                r.push({ x: musicFlyout.x - m, y: musicFlyout.baseY - 8,
                         width: musicFlyout.width + 2 * m, height: musicTargetH + 2 * m + 8, radius: 30 })
            }
            if (hubVisible) {
                r.push({ x: hubFlyout.x - m, y: hubFlyout.baseY - 8,
                         width: hubFlyout.width + 2 * m, height: hubTargetH + 2 * m + 8, radius: 24 })
            }
            if (paletteVisible) {
                r.push({ x: paletteFlyout.x - m, y: paletteFlyout.baseY - 8,
                         width: paletteFlyout.width + 2 * m, height: paletteTargetH + 2 * m + 8, radius: 24 })
            }
            if (previewShown) {
                r.push({ x: previewCard.x - m, y: previewCard.y - 8,
                         width: previewCard.width + 2 * m, height: previewCard.height + 2 * m + 8, radius: 24 })
            }
        }
        pushBuddyRegions(r)
        Overlay.setInteractiveRegions(r)
    }

    Component.onCompleted: updateMask()

    // TEMP-BUDDY-DEMO-BEGIN (verification only; removed after testing)
    Timer {
        id: buddyDemo
        property int step: 0
        property var bub: null
        property var sp: null
        property var saved: null
        readonly property string out: "C:/Users/ASIFCO~1/AppData/Local/Temp/claude/C--Users-Asif-Computer-Desktop-noch/cf5eb338-20af-4e33-8338-7aa4de60352f/scratchpad/shots2/"
        running: Qt.application.arguments.indexOf("--buddy-demo") >= 0
        interval: 1500
        function grab(name) {
            console.log("DEMO", name, Math.round(buddy.x), Math.round(buddy.y))
            root.contentItem.grabToImage(function(r) { r.saveToFile(out + name + ".png") })
        }
        function find(item, pred) {
            if (!item) return null
            if (pred(item)) return item
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], pred); if (f) return f }
            return null
        }
        function next(ms) { interval = ms; restart() }
        onTriggered: {
            if (!bub) bub = find(buddy, function(o) { return o.objectName === "buddyBubble" })
            switch (step++) {
            case 0:
                saved = { h: cfg.buddyHistory }
                const now = Date.now()
                cfg.buddyHistory = JSON.stringify([
                    { q: "How do I stay focused in the afternoon?", kind: "chat", title: "", a: "Try a **25-minute** focus block, a short walk, and water before coffee.", t: now - 120000 },
                    { q: "chart btc", kind: "chart", title: "BTC sample chart", a: "", t: now - 3 * 3600000 },
                    { q: "plan my day", kind: "tasks", title: "Today's plan", a: "", t: now - 26 * 3600000 },
                    { q: "What's a good name for a cat?", kind: "chat", title: "", a: "Miso, Pixel or Biscuit - all short and friendly.", t: now - 5 * 86400000 }
                ])
                buddy.openInput(); next(900); break
            case 1: grab("h0_input"); bub.showHistory(); next(160); break
            case 2: grab("h1_hist_anim"); next(800); break
            case 3: grab("h2_hist"); bub.pickHistory(0); next(1600); break
            case 4: grab("h3_replay"); buddy.dismiss(); next(700); break
            case 5: buddy.openInput(); next(900); break
            case 6: grab("t1_input"); buddy.dismiss(); buddy.setMood("happy", 5000); next(600); break
            case 7: grab("t2_ring"); root.toggleSettings(); next(900); break
            case 8: sp = find(root.contentItem, function(o) { return o.appFilter !== undefined && o.tab !== undefined }); grab("s1_general"); next(100); break
            case 9: { const f = find(sp, function(o) { return o.contentHeight !== undefined && o.visible && o.contentHeight > o.height }); if (f) f.contentY = f.contentHeight - f.height; next(500); break }
            case 10: grab("s1b_general_scrolled"); sp.tab = 2; next(600); break
            case 11: grab("s2_buddy"); next(700); break
            case 12: next(100); break
            case 13:
                cfg.buddyHistory = saved.h
                root.toggleSettings(); console.log("DEMO restored"); break
            }
        }
    }
    // TEMP-BUDDY-DEMO-END

    // ------------------------------------------------------------ peek dash
    // While tucked away, only this centered dash at the screen edge catches
    // the pointer (and file drags) to pull the notch out.
    PeekDash {
        id: peekZone
        root: root
    }

    // ------------------------------------------------------------ notch
    Item {
        id: pill
        width: root.animatedPillW
        height: root.pillH
        x: (root.width - width) / 2
        y: root.hiddenY + (root.shownY - root.hiddenY) * root.progress
        opacity: root.clamp01(root.progress * 4)
        visible: opacity > 0.01
        scale: 0.72 + 0.28 * root.clamp01(root.progress)
        transformOrigin: Item.Top

        HoverHandler { id: pillHover }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            target: null
            onWheel: (event) => root.setScale(cfg.sizeScale + (event.angleDelta.y / 120) * 0.05)
        }
        Glass {
            id: bg
            anchors.fill: parent
            radius: height / 2
            edge: root.dragging ? Theme.accent : "transparent"
        }

        // Clicking empty notch space dismisses menus; right-click opens settings.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    root.toggleSettings()
                else
                    root.closeAll()
            }
        }

        Item {
            id: contentClip
            anchors.fill: parent

            Row {
                id: dockRow
                anchors.centerIn: parent
                height: parent.height

                // Apps (Left side of dock)
                Item {
                    id: appsArea
                    width: appsRow.width
                    height: parent.height

                    HoverHandler {
                        id: appsHover
                        onHoveredChanged: {
                            if (!hovered) {
                                previewShowTimer.stop()
                                previewHideTimer.restart()
                            }
                        }
                    }

                    Row {
                        id: appsRow
                        height: parent.height

                        Repeater {
                            model: root.apps
                            delegate: DockItem {
                                id: dockItem
                                height: appsRow.height
                                s: root.s
                                cellW: root.cellW
                                iconSize: root.iconSize
                                mono: cfg.monoIcons
                                pointerX: appsHover.hovered ? appsHover.point.position.x : -1
                                appear: root.itemAppear(index + 1)
                                running: root.running.indexOf(modelData.exe) >= 0
                                menuActive: root.menuIndex === index

                                onHoveredChanged: {
                                    const centerX = dockItem.mapToItem(root.contentItem, width / 2, 0).x
                                    if (hovered) {
                                        root.hoverIndex = index
                                        root.tipX = centerX
                                        if (!root.tipReady)
                                            tipTimer.restart()
                                        if (dockItem.running && modelData.exe) {
                                            root.requestPreview(modelData.exe, centerX)
                                        } else {
                                            previewShowTimer.stop()
                                            root.previewOpen = false
                                        }
                                    } else if (root.hoverIndex === index) {
                                        root.hoverIndex = -1
                                        tipTimer.stop()
                                        tipGrace.restart()
                                    }
                                }
                                onActivated: (i) => root.activateOrLaunchApp(i)
                                onContextRequested: (i) => {
                                    root.previewOpen = false
                                    root.openMenu(i, dockItem.mapToItem(root.contentItem, width / 2, 0).x)
                                }
                            }
                        }

                        // Drop target placeholder.
                        Item {
                            width: root.dragging ? root.cellW : 0
                            height: parent.height
                            clip: true
                            Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                            Rectangle {
                                anchors.centerIn: parent
                                width: root.iconSize
                                height: width
                                radius: width * 0.28
                                color: "transparent"
                                border.width: 1.5
                                border.color: Theme.accent
                                Glyph {
                                    anchors.centerIn: parent
                                    name: "plus"
                                    size: root.iconSize * 0.55
                                    stroke: 2
                                    color: Theme.accent
                                }
                            }
                        }

                        // Empty dock prompt — clicking opens Settings > Apps list.
                        Item {
                            visible: root.apps.length === 0 && !root.dragging
                            width: emptyPromptRow.implicitWidth + Math.round(16 * root.s)
                            height: parent.height

                            Row {
                                id: emptyPromptRow
                                anchors.centerIn: parent
                                spacing: Math.round(6 * root.s)

                                Glyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "plus"
                                    size: Math.round(14 * root.s)
                                    stroke: 2
                                    color: emptyPromptHit.containsMouse ? Theme.ink : Theme.textDim
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Add apps")
                                    color: emptyPromptHit.containsMouse ? Theme.ink : Theme.textDim
                                    font.family: Theme.font
                                    font.pixelSize: Math.round(12.5 * root.s)
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: emptyPromptHit
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    panelItem.tab = 1
                                    root.panelOpen = true
                                }
                            }
                        }
                    }
                }

                // Vertical divider between apps and status block
                Item {
                    visible: status.visible
                    width: Math.round(16 * root.s)
                    height: parent.height
                    opacity: status.appear

                    Rectangle {
                        anchors.centerIn: parent
                        width: 1
                        height: Math.round(20 * root.s)
                        color: Theme.border
                    }
                }

                // Clock (time over date, right side of the dock)
                StatusBlock {
                    id: status
                    s: root.s
                    showClock: cfg.showClock
                    active: root.hubOpen
                    appear: root.itemAppear(root.apps.length + 1)
                    onClicked: root.toggleHub(-1)
                    onSettingsRequested: root.toggleSettings()
                    onHintRequested: (text) => {
                        root.hoverHint = text
                        root.hoverIndex = -1
                        root.tipX = status.mapToItem(root.contentItem, status.width / 2, 0).x
                        if (!root.tipReady)
                            tipTimer.restart()
                    }
                    onHintCleared: {
                        root.hoverHint = ""
                        tipTimer.stop()
                        tipGrace.restart()
                    }
                }
            }
        }

        DropArea {
            id: pillDrop
            anchors.fill: parent
            onDropped: (drop) => root.handleDrop(drop)
        }
    }

    function handleDrop(drop) {
        const result = Player.ingestDrop(drop.urls)
        const added = result.added || 0
        let pinned = 0
        const others = result.other || []
        for (let i = 0; i < others.length; ++i) {
            if (Dock.pinUrl(others[i]))
                ++pinned
        }
        if (added > 0) {
            root.showToast(added === 1 ? qsTr("Added to player") : qsTr("Added %1 songs").arg(added))
            root.panelOpen = false
            root.hubOpen = false
            root.menuIndex = -1
            root.musicOpen = true
        } else if (pinned > 0) {
            root.showToast(pinned === 1 ? qsTr("Pinned") : qsTr("Pinned %1 apps").arg(pinned))
        } else if (added === 0 && pinned === 0 && (result.folders || 0) === 0 && drop.hasUrls) {
            // Nothing anywhere knew how to use: say so instead of eating the drop.
            root.showToast(qsTr("Nothing here to add or pin"))
        }
        drop.accept(Qt.CopyAction)
    }

    // Resolve the row the app context menu was opened for, by identity. A
    // list that changes while the menu is up must never make index 2 mean
    // "whatever app currently sits at index 2".
    function menuRowIndex() {
        if (!menu || !menu.shownApp)
            return -1
        const key = menu.shownApp.path || ""
        const name = menu.shownApp.name || ""
        for (let i = 0; i < root.apps.length; ++i)
            if ((root.apps[i].path || "") === key && (root.apps[i].name || "") === name)
                return i
        return -1
    }

    // ------------------------------------------------------------ music pill
    // Separate pill to the right of the notch: [♫] or [ |◀ ▶ ▶| ].
    Item {
        id: musicDock
        width: nowPlaying.width
        height: root.pillH
        x: pill.x + pill.width + root.musicGap
        y: pill.y
        scale: 0.85 + 0.15 * root.clamp01(root.progress)
        transformOrigin: Item.Top

        HoverHandler { id: musicPillHover }

        NowPlaying {
            id: nowPlaying
            s: root.s
            active: root.musicOpen
            highlight: musicDrop.containsDrag
            appear: root.clamp01(root.progress * 1.4 - 0.2)
            onOpenRequested: root.toggleMusic()
            onVolumeNudged: (v) => root.showToast(qsTr("Volume %1%").arg(Math.round(v * 100)))
            onHintRequested: (text) => {
                root.hoverHint = text
                root.hoverIndex = -1
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
        x: musicDock.x + musicDock.width + root.musicGap
        y: pill.y
        transformOrigin: Item.Left

        onClicked: {
            root.timerAlert = false
            if (sessionTimer.finished)
                sessionTimer.reset()
            root.openHub(0)
        }
        onHintRequested: (text) => {
            root.hoverHint = text
            root.hoverIndex = -1
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

    // ------------------------------------------------------------ open apps
    // Programs with a window open: their own pill on the left of the notch,
    // mirroring the music pill on the right. Pinned apps stay in the notch.
    // Hovering a tile previews its windows; clicking one brings it forward.
    property bool previewOpen: false
    property string previewExe: ""
    property string previewName: ""
    property int previewCount: 0
    property var previewWindows: []
    property real previewCenterX: 0
    property real previewStamp: 0
    readonly property bool previewShown: revealed && previewOpen && previewWindows.length > 0
                                         && !panelOpen && !musicOpen && !hubOpen
                                         && !paletteOpen && menuIndex < 0

    function requestPreview(exe, centerX) {
        previewHideTimer.stop()
        if (previewOpen) {
            if (previewExe !== exe)
                openPreview(exe, centerX)
            else
                previewCenterX = centerX
        } else {
            previewShowTimer.exe = exe
            previewShowTimer.centerX = centerX
            previewShowTimer.restart()
        }
    }

    function openPreview(exe, centerX) {
        const windows = OpenApps.windowsFor(exe)
        if (windows.length === 0) {
            previewOpen = false
            return
        }
        const program = openList.filter(app => app.exe === exe)[0]
        const pinned = apps.filter(app => app.exe === exe)[0]
        previewExe = exe
        previewName = (pinned && pinned.name) ? pinned.name : (program ? program.name : exe)
        previewCount = windows.length
        previewWindows = windows.slice(0, 3)
        previewCenterX = centerX
        previewStamp = Date.now()
        previewOpen = true
    }

    // A tile opens its preview after a short pause, so sweeping across the row
    // does not flash every preview. Leaving the row and the preview closes it.
    Timer {
        id: previewShowTimer
        interval: 260
        property string exe: ""
        property real centerX: 0
        onTriggered: root.openPreview(exe, centerX)
    }
    Timer {
        id: previewHideTimer
        interval: 260
        onTriggered: {
            if (!previewCard.hovered && !appsHover.hovered)
                root.previewOpen = false
        }
    }

    WindowPreview {
        id: previewCard
        programName: root.previewName
        windowCount: root.previewCount
        windows: root.previewWindows
        stamp: root.previewStamp
        anchorX: root.previewCenterX
        containerWidth: root.width
        cardTop: root.shownY + root.pillH + 12
        opacity: root.previewShown ? 1 : 0
        scale: root.previewShown ? 1 : 0.96
        visible: opacity > 0.01
        transformOrigin: Item.Top

        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on x { enabled: previewCard.opacity > 0.5; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        onActivated: (windowId) => {
            root.previewOpen = false
            OpenApps.activateWindow(windowId)
        }
        onHoveredChanged: {
            if (hovered)
                previewHideTimer.stop()
            else
                previewHideTimer.restart()
        }
    }

    // ------------------------------------------------------------ resize grip
    ResizeGrip {
        id: grip
        root: root
        cfg: cfg
        pillItem: pill
        pillHovered: pillHover.hovered
    }

    // ------------------------------------------------------------ tooltip / toast / size HUD
    TooltipToast {
        id: tip
        root: root
        cfg: cfg
    }

    // ------------------------------------------------------------ app context menu
    AppContextMenu {
        id: menu
        root: root
    }

    // ------------------------------------------------------------ settings panel
    SettingsFlyout {
        id: panelItem
        root: root
        cfg: cfg
        fileDialog: fileDialog
    }

    // ------------------------------------------------------------ music player
    MusicFlyout {
        id: musicFlyout
        objectName: "musicFlyout"
        root: root
        musicDialog: musicDialog
    }

    // ------------------------------------------------------------ hub
    // Timer and Clipboard in one card, opened from the clock.
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

    Buddy {
        id: buddy
        visible: cfg.buddyEnabled
        solidRing: cfg.buddySolidRing
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
    Binding { target: BuddyAgent; property: "permissionMode"; value: cfg.buddyPermissionMode }
    Binding { target: BuddyAgent; property: "workspaceRoot"; value: cfg.buddyWorkspaceRoot }
    Binding { target: BuddyAgent; property: "redactSecrets"; value: cfg.buddyRedactSecrets }
    Binding { target: BuddyAgent; property: "keepTraces"; value: cfg.buddyKeepTraces }
    Binding { target: BuddyAgent; property: "streamReplies"; value: cfg.buddyStreamReplies }
    Binding { target: BuddyAgent; property: "handoffs"; value: cfg.buddyHandoffs }
    Binding { target: BuddyAgent; property: "officialComputerTools"; value: cfg.buddyOfficialComputer }
    Binding { target: BuddyAgent; property: "computerDetail"; value: cfg.buddyComputerDetail }
    Binding { target: BuddyAgent; property: "maxSteps"; value: cfg.buddyMaxSteps }
    Binding { target: BuddyAgent; property: "allowAskUser"; value: cfg.buddyAllowAskUser }
    Binding { target: Voice; property: "enabled"; value: cfg.buddyVoiceEnabled }
    Binding { target: Voice; property: "apiBase"; value: cfg.buddyVoiceBase }
    // A speech service of its own needs its own key; one left empty only works
    // for a service on this computer. The chat key is never sent to it.
    Binding { target: Voice; property: "apiKey"; value: cfg.buddyVoiceKey }
    Binding { target: Voice; property: "model"; value: cfg.buddyVoiceModel }
    Binding { target: Voice; property: "language"; value: cfg.buddyVoiceLanguage }
    Binding { target: TalkKey; property: "enabled"; value: cfg.buddyVoiceEnabled && cfg.buddyVoiceHotkey }
    Binding { target: BuddySound; property: "enabled"; value: cfg.buddySfxEnabled }
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

    FileDialog {
        id: fileDialog
        title: qsTr("Pin an app to Noch")
        currentFolder: StandardPaths.writableLocation(StandardPaths.ApplicationsLocation)
        nameFilters: [qsTr("Apps and shortcuts (*.exe *.lnk *.url *.bat *.cmd)"), qsTr("All files (*)")]
        onAccepted: {
            if (Dock.pinUrl(selectedFile))
                root.showToast(qsTr("Pinned"))
        }
    }

    FileDialog {
        id: musicDialog
        title: qsTr("Add music")
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.MusicLocation)
        nameFilters: [qsTr("Audio (*.mp3 *.flac *.wav *.m4a *.aac *.ogg *.opus *.wma *.aiff *.aif)")]
        onAccepted: {
            const n = Player.addUrls(selectedFiles)
            if (n > 0) {
                root.showToast(n === 1 ? qsTr("Added to player") : qsTr("Added %1 songs").arg(n))
                root.panelOpen = false
                root.hubOpen = false
                root.menuIndex = -1
                root.musicOpen = true
            }
        }
    }
}
