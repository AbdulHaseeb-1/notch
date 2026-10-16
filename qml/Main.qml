import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtCore

Window {
    id: root

    visible: true
    color: "transparent"
    title: qsTr("Noch")
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    Theme { id: t }

    Settings {
        id: cfg
        category: "ui"
        property real sizeScale: 1.0
        property bool autoHide: true
        property int hideDelay: 1200
        property bool monoIcons: true
        property bool showClock: true
        property bool showStats: false
        property int focusMinutes: 25
        property bool hotkeyPalette: true
        property bool suppressInFullscreen: true
        property bool buddyEnabled: true
        property real buddyX: -1
        property string buddyPlan: ""
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
    readonly property bool fullscreenGuard: cfg.suppressInFullscreen && fullscreen.active && !paletteOpen
    readonly property bool pointerInside: !fullscreenGuard
                                          && (revealed
                                              ? (pillHover.hovered || peekZone.containsMouse
                                                 || grip.containsMouse || panelItem.hovered
                                                 || menu.hovered || musicFlyout.hovered
                                                 || hubFlyout.hovered || musicPillHover.hovered
                                                 || openRow.hovered || previewCard.hovered || timerPill.hovered
                                                 || paletteFlyout.hovered)
                                              : peekZone.containsMouse)
    readonly property bool dragging: peekDrop.containsDrag || pillDrop.containsDrag
                                     || musicDrop.containsDrag
    readonly property bool holdOpen: !cfg.autoHide || panelOpen || musicOpen || hubOpen || paletteOpen
                                     || menuIndex >= 0 || resizing || launchHold || introHold
                                     || dragging || fileDialog.visible || musicDialog.visible || timerAlert
    readonly property bool wantVisible: pointerInside || holdOpen

    onWantVisibleChanged: syncReveal()

    function syncReveal() {
        if (wantVisible) {
            hideTimer.stop()
            revealed = true
        } else {
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: cfg.hideDelay
        onTriggered: if (!root.wantVisible) root.revealed = false
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
    readonly property var apps: dock.apps
    readonly property var running: dock.running
    readonly property var openList: openApps.apps
    readonly property var unpinnedOpenList: {
        const pinnedExes = {}
        for (let i = 0; i < apps.length; ++i) {
            if (apps[i].exe)
                pinnedExes[apps[i].exe.toLowerCase()] = true
        }
        return openList.filter(a => !a.exe || !pinnedExes[a.exe.toLowerCase()])
    }

    function activateOrLaunchApp(i) {
        if (i < 0 || i >= apps.length)
            return
        previewOpen = false
        const app = apps[i]
        if (app.exe && running.indexOf(app.exe) >= 0 && openApps.activateApp(app.exe)) {
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
        if (dock.launch(i))
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

    function closeAll() {
        panelOpen = false
        musicOpen = false
        hubOpen = false
        paletteOpen = false
        menuIndex = -1
        buddy.dismiss()
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
        paletteCard.takeFocus()
    }

    function closePalette() {
        paletteOpen = false
    }

    // Tells the user which shortcut the palette listens on when the preferred
    // one was already taken at startup.
    function hotkeyNotice() {
        if (!cfg.hotkeyPalette)
            return
        if (!hotkey.registered)
            showToast(qsTr("Palette hotkey unavailable"))
        else if (!hotkey.preferred)
            showToast(qsTr("Ctrl+Alt+Space is taken — press %1 for the palette").arg(hotkey.shortcut))
    }

    // The built-in commands the palette's list can offer.
    function runCommand(id) {
        switch (id) {
        case "lock":
            power.lock()
            break
        case "sleep":
            power.sleep()
            break
        case "mic":
            audio.toggleMicMute()
            showToast(audio.micMuted ? qsTr("Microphone muted") : qsTr("Microphone live"))
            break
        case "taskmgr":
            search.openPath("taskmgr.exe")
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

    onActiveChanged: if (!active && !fileDialog.visible && !musicDialog.visible) closeAll()

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
    readonly property real panelTargetH: settings.targetHeight
    readonly property real hubTargetH: hubPanel.targetHeight
    readonly property real musicTargetH: musicPanel.targetHeight
    readonly property real paletteTargetH: paletteCard.targetHeight
    readonly property var openMenuRect: openRow.menuRectIn(root.contentItem)
    readonly property var maskKey: [revealed, fullyHidden, Math.round(pillW), Math.round(pillH), width,
                                    Math.round(musicDock.width), Math.round(timerPill.width),
                                    panelVisible, Math.round(panelTargetH), menuVisible,
                                    Math.round(menu.x), Math.round(menu.height),
                                    musicVisible, Math.round(musicFlyout.x), Math.round(musicTargetH),
                                    hubVisible, Math.round(hubTargetH),
                                    paletteVisible, Math.round(paletteFlyout.width),
                                    Math.round(paletteTargetH),
                                    tipVisible, hudVisible,
                                    openRow.visible, Math.round(openRow.x),
                                    openRow.menuShown, Math.round(openMenuRect.x),
                                    Math.round(openMenuRect.height),
                                    previewShown, Math.round(previewCard.x),
                                    Math.round(previewCard.width), Math.round(previewCard.height),
                                    cfg.buddyEnabled, buddy.session, Math.round(buddy.anchor),
                                    buddy.inputShown, Math.round(buddy.inputItem.height),
                                    buddyPanel.open, buddyPanel.visible,
                                    Math.round(buddyPanel.dragX), Math.round(buddyPanel.dragY),
                                    Math.round(buddyPanel.width), Math.round(buddyPanel.height)]
    onMaskKeyChanged: Qt.callLater(updateMask)

    // The buddy's regions exist whether or not the notch is tucked away: the
    // creature lives independently at the top edge. Main owns this because the
    // result panel is a window-level card, like the flyouts above.
    function pushBuddyRegions(r) {
        if (!cfg.buddyEnabled || !buddy.visible)
            return
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
            const notchLeft = (width - pillW) / 2 - m
            const left = openRow.visible ? Math.min(notchLeft, openRow.x - m) : notchLeft
            const rightEdge = timerPill.visible ? timerPill.x + timerPill.width
                                                : (width + pillW) / 2 + musicGap + musicDock.width
            const right = Math.max(rightEdge, (width + pillW) / 2 + musicGap + musicDock.width) + m
            const below = (tipVisible || hudVisible) ? 52 : m
            r.push({ x: left, y: 0, width: right - left, height: shownY + pillH + below, radius: 26 })
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
            if (openRow.menuShown) {
                const om = openMenuRect
                r.push({ x: om.x - m, y: om.y - m,
                         width: om.width + 2 * m, height: om.height + 2 * m + 8, radius: 18 })
            }
            if (previewShown) {
                r.push({ x: previewCard.x - m, y: previewCard.y - 8,
                         width: previewCard.width + 2 * m, height: previewCard.height + 2 * m + 8, radius: 24 })
            }
        }
        pushBuddyRegions(r)
        overlay.setInteractiveRegions(r)
        updateGlass()
    }

    // The visible glass shapes, for the optional real blur (Settings > Blur
    // behind). Only worked out while that is on.
    function glassShape(item, radius) {
        const p = item.mapToItem(null, 0, 0)
        return { x: p.x, y: p.y, width: item.width * item.scale, height: item.height * item.scale, radius: radius }
    }

    function updateGlass() {
        if (!glassBackdrop.enabled)
            return
        const g = []
        if (!fullyHidden) {
            g.push(glassShape(pill, pill.height / 2))
            if (musicDock.visible)
                g.push(glassShape(musicDock, musicDock.height / 2))
            if (openRow.visible)
                g.push(glassShape(openRow, openRow.height / 2))
            if (timerPill.visible)
                g.push(glassShape(timerPill, timerPill.height / 2))
            if (panelVisible) g.push(glassShape(panelItem, 22))
            if (menuVisible) g.push(glassShape(menu, 18))
            if (musicVisible) g.push(glassShape(musicFlyout, 28))
            if (hubVisible) g.push(glassShape(hubFlyout, 22))
            if (paletteVisible) g.push(glassShape(paletteFlyout, 22))
            if (openRow.menuShown) {
                const om = openMenuRect
                g.push({ x: om.x, y: om.y, width: om.width, height: om.height, radius: 16 })
            }
            if (previewShown) g.push(glassShape(previewCard, 22))
        }
        // The buddy's glass pieces stand outside the notch's hidden state.
        if (cfg.buddyEnabled && buddy.visible) {
            if (buddy.inputShown)
                g.push(glassShape(buddy.inputItem, 21))
            if (buddyPanel.open)
                g.push(glassShape(buddyPanel, 20))
        }
        glassBackdrop.setShapes(g)
    }

    // While the blur is on, keep its shapes in step with the animations
    // (a few updates a second is plenty; nothing runs when it is off).
    Timer {
        interval: 33
        repeat: true
        running: glassBackdrop.enabled && root.visible
        onTriggered: root.updateGlass()
    }
    Connections {
        target: glassBackdrop
        function onEnabledChanged() { root.updateGlass() }
    }

    Component.onCompleted: updateMask()

    // ------------------------------------------------------------ peek dash
    // While tucked away, only this centered dash at the screen edge catches
    // the pointer (and file drags) to pull the notch out.
    MouseArea {
        id: peekZone
        width: root.dashW + Math.round(6 * root.s)
        height: root.revealed ? (root.shownY + 2) : Math.max(7, root.dashH + 4)
        x: Math.round((root.width - width) / 2)
        y: 0
        hoverEnabled: true
        onClicked: if (!root.fullscreenGuard) root.revealed = true

        Rectangle {
            id: peekBar
            anchors.horizontalCenter: parent.horizontalCenter
            y: 2
            width: root.dashW
            height: root.dashH
            radius: height / 2
            color: peekZone.containsMouse ? t.ink : t.textDim
            opacity: root.clamp01(1 - root.progress * 3.5)
            Behavior on color { ColorAnimation { duration: 150 } }
        }

        DropArea {
            id: peekDrop
            anchors.fill: parent
            // A drag over the tucked-away dash used to be dropped and lost;
            // route it through the same ingestion path as everything else.
            onDropped: (drop) => root.handleDrop(drop)
        }
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
            edge: root.dragging ? t.ink : "transparent"
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
                                border.color: t.ink
                                Glyph {
                                    anchors.centerIn: parent
                                    name: "plus"
                                    size: root.iconSize * 0.55
                                    stroke: 2
                                    color: t.ink
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
                                    color: emptyPromptHit.containsMouse ? t.ink : t.textDim
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Add apps")
                                    color: emptyPromptHit.containsMouse ? t.ink : t.textDim
                                    font.family: t.font
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
                                    settings.tab = 1
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
                        color: t.border
                    }
                }

                // Clock (time over date, right side of the dock)
                StatusBlock {
                    id: status
                    s: root.s
                    showClock: cfg.showClock
                    showStats: cfg.showStats
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
        const result = player.ingestDrop(drop.urls)
        const added = result.added || 0
        let pinned = 0
        const others = result.other || []
        for (let i = 0; i < others.length; ++i) {
            if (dock.pinUrl(others[i]))
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
            root.openHub(1)
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
        const windows = openApps.windowsFor(exe)
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
            if (!openRow.hovered && !previewCard.hovered && !appsHover.hovered)
                root.previewOpen = false
        }
    }

    OpenApps {
        id: openRow
        x: pill.x - root.musicGap - width
        y: pill.y
        s: root.s
        mono: cfg.monoIcons
        apps: root.unpinnedOpenList
        appear: root.clamp01(root.progress * 1.4 - 0.2)
        scale: 0.85 + 0.15 * root.clamp01(root.progress)
        transformOrigin: Item.Top

        onActivated: (exe) => {
            root.previewOpen = false
            openApps.activateApp(exe)
        }
        onMenuShownChanged: if (menuShown) root.previewOpen = false
        onPreviewRequested: (exe, centerX) => root.requestPreview(exe, centerX)
        onHoveredChanged: {
            if (hovered) {
                previewHideTimer.stop()
            } else {
                previewShowTimer.stop()
                previewHideTimer.restart()
            }
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
            openApps.activateWindow(windowId)
        }
        onHoveredChanged: {
            if (hovered)
                previewHideTimer.stop()
            else
                previewHideTimer.restart()
        }
    }

    // ------------------------------------------------------------ resize grip
    MouseArea {
        id: grip
        width: Math.round(64 * root.s)
        height: 16
        x: (root.width - width) / 2
        y: pill.y + pill.height - 6
        hoverEnabled: true
        enabled: root.revealed
        cursorShape: Qt.SizeVerCursor
        preventStealing: true

        property real startY: 0
        property real startScale: 1

        onPressed: (mouse) => {
            startY = mapToItem(null, mouse.x, mouse.y).y
            startScale = cfg.sizeScale
            root.resizing = true
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const dy = mapToItem(null, mouse.x, mouse.y).y - startY
            root.setScale(startScale + dy / 58)
        }
        onReleased: { root.resizing = false; hudTimer.restart() }
        onCanceled: root.resizing = false

        readonly property bool showBar: containsMouse || pressed
            || (pillHover.hovered && !root.tipVisible && root.menuIndex < 0)

        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 0
            width: grip.containsMouse || grip.pressed ? 46 : 30
            height: grip.pressed ? 5 : 4
            radius: height / 2
            color: grip.containsMouse || grip.pressed ? t.ink : t.textDim
            border.width: 1
            border.color: t.surface
            opacity: grip.showBar && root.progress > 0.9 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }
    }

    // ------------------------------------------------------------ tooltip / toast / size HUD
    Rectangle {
        id: tip
        readonly property bool showHud: root.hudVisible
        readonly property string label: showHud ? Math.round(cfg.sizeScale * 100) + "%" : root.tipText

        width: Math.min(root.width - 24, tipLabel.implicitWidth + 20)
        height: 26
        radius: 13
        color: showHud ? t.surface : t.ink
        border.width: showHud ? 1 : 0
        border.color: t.borderStrong
        x: Math.round(Math.max(12, Math.min(root.width - width - 12,
               (showHud || root.toast.length > 0 ? root.width / 2 : root.tipX) - width / 2)))
        y: root.shownY + root.pillH + 12 + (shown ? 0 : -4)
        opacity: shown ? 1 : 0
        scale: shown ? 1 : 0.94
        readonly property bool shown: showHud || root.tipVisible

        Behavior on opacity { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        Behavior on x { enabled: tip.opacity > 0.5; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        Text {
            id: tipLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, tip.width - 20)
            elide: Text.ElideRight
            text: tip.label
            color: tip.showHud ? t.text : t.inverse
            font.family: t.font
            font.pixelSize: 12
            font.weight: Font.Bold
        }
    }

    // ------------------------------------------------------------ app context menu
    Flyout {
        id: menu
        open: root.menuIndex >= 0
        radius: 18
        baseY: root.shownY + root.pillH + 10
        property bool renaming: false
        // Snapshot of the app this menu belongs to; rows resolve the live
        // index through root.menuRowIndex().
        property var shownApp: null
        readonly property var app: shownApp

        onOpenChanged: {
            if (open) {
                shownApp = root.menuIndex >= 0 ? root.apps[root.menuIndex] : null
                renaming = false
            }
        }
        Connections {
            target: root
            function onMenuIndexChanged() {
                if (root.menuIndex >= 0) {
                    menu.shownApp = root.apps[root.menuIndex]
                    menu.renaming = false
                }
            }
        }

        width: 280
        height: menuCol.implicitHeight + 12
        x: Math.round(Math.max(12, Math.min(root.width - width - 12, root.menuX - width / 2)))

        Column {
            id: menuCol
            y: 6
            width: parent.width

            Item {
                width: parent.width
                height: 52
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        visible: !menu.renaming
                        width: parent.width
                        text: menu.app ? menu.app.name : ""
                        color: t.ink
                        elide: Text.ElideRight
                        font.family: t.font
                        font.pixelSize: 15
                        font.weight: Font.ExtraBold
                    }
                    Rectangle {
                        visible: menu.renaming
                        width: parent.width
                        height: 26
                        radius: 6
                        color: t.raised
                        border.width: 1
                        border.color: t.ink
                        TextInput {
                            id: renameInput
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            color: t.ink
                            selectionColor: t.ink
                            selectedTextColor: t.inverse
                            font.family: t.font
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            clip: true
                            onAccepted: {
                                const i = root.menuRowIndex()
                                if (i >= 0)
                                    dock.rename(i, text)
                                menu.renaming = false
                            }
                            Keys.onEscapePressed: menu.renaming = false
                        }
                    }
                    Text {
                        width: parent.width
                        text: menu.app ? (root.running.indexOf(menu.app.exe) >= 0 ? qsTr("Running") : (menu.app.path || "")) : ""
                        color: t.textDim
                        elide: Text.ElideMiddle
                        font.family: t.font
                        font.pixelSize: 11
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: t.borderSoft }
            Item { width: 1; height: 4 }

            MenuRow {
                width: parent.width
                label: qsTr("Open")
                icon: "open"
                hint: "↵"
                onTriggered: {
                    const i = root.menuRowIndex()
                    if (i >= 0)
                        root.launchApp(i)
                }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Rename")
                icon: "edit"
                onTriggered: {
                    renameInput.text = menu.app ? menu.app.name : ""
                    menu.renaming = true
                    renameInput.forceActiveFocus()
                    renameInput.selectAll()
                }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Show in folder")
                icon: "folder"
                visible: !!(menu.app && menu.app.path)
                onTriggered: {
                    const i = root.menuRowIndex()
                    if (i >= 0)
                        dock.revealInFolder(i)
                    root.menuIndex = -1
                }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Move left")
                icon: "left"
                readonly property int liveIndex: root.menuRowIndex()
                visible: liveIndex > 0
                onTriggered: { dock.move(liveIndex, liveIndex - 1); root.menuIndex = -1 }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Move right")
                icon: "right"
                readonly property int liveIndex: root.menuRowIndex()
                visible: liveIndex >= 0 && liveIndex < root.apps.length - 1
                onTriggered: { dock.move(liveIndex, liveIndex + 1); root.menuIndex = -1 }
            }
            Item { width: 1; height: 4 }
            Rectangle { width: parent.width; height: 1; color: t.borderSoft }
            Item { width: 1; height: 4 }
            MenuRow {
                width: parent.width
                label: qsTr("Unpin")
                icon: "unpin"
                danger: true
                onTriggered: {
                    const i = root.menuRowIndex()
                    root.menuIndex = -1
                    if (i >= 0)
                        dock.unpin(i)
                }
            }
        }
    }

    // ------------------------------------------------------------ settings panel
    Flyout {
        id: panelItem
        open: root.panelOpen
        radius: 22
        baseY: root.shownY + root.pillH + 12

        width: settings.implicitWidth
        height: settings.implicitHeight
        x: Math.round((root.width - width) / 2)

        SettingsPanel {
            id: settings
            anchors.fill: parent
            prefs: cfg
            dockModel: root.apps
            runningList: root.running
            version: dock.version
            startupSupported: dock.startupSupported
            launchAtStartup: dock.launchAtStartup
            blurSupported: glassBackdrop.supported
            blurBehind: glassBackdrop.enabled
            maxBodyHeight: root.height - (root.shownY + root.pillH + 12) - settings.headerH - settings.footerH - 24

            onCloseRequested: root.panelOpen = false
            onAddRequested: fileDialog.open()
            onPinAppRequested: (name, path) => {
                if (dock.pinApp(name, path))
                    root.showToast(qsTr("Pinned %1").arg(name))
            }
            onResetRequested: dock.resetToDefaults()
            onQuitRequested: dock.quit()
            onUnpinRequested: (i) => dock.unpin(i)
            onMoveRequested: (from, to) => dock.move(from, to)
            onStartupToggled: (on) => dock.launchAtStartup = on
            onBlurToggled: (on) => glassBackdrop.enabled = on
            lightTheme: appearance.light
            onThemeToggled: (on) => appearance.light = on
        }
    }

    // ------------------------------------------------------------ music player
    Flyout {
        id: musicFlyout
        objectName: "musicFlyout"
        open: root.musicOpen
        radius: 28
        baseY: root.shownY + root.pillH + 12

        width: musicPanel.implicitWidth
        height: musicPanel.implicitHeight
        x: Math.round(Math.max(12, (root.width - width) / 2))

        MusicPanel {
            id: musicPanel
            anchors.fill: parent
            onCloseRequested: root.musicOpen = false
            onAddRequested: musicDialog.open()
            onToastRequested: (text) => root.showToast(text)
            onDropForwarded: (drop) => root.handleDrop(drop)
        }
    }

    // ------------------------------------------------------------ hub
    // System, Timer and Clipboard in one card, opened from the clock.
    Flyout {
        id: hubFlyout
        objectName: "hubFlyout"
        open: root.hubOpen
        radius: 22
        baseY: root.shownY + root.pillH + 12

        width: hubPanel.implicitWidth
        height: hubPanel.implicitHeight
        x: Math.round(Math.max(12, (root.width - width) / 2))

        HubPanel {
            id: hubPanel
            anchors.fill: parent
            tab: root.hubTab
            focusTimer: sessionTimer
            maxBodyHeight: root.height - (root.shownY + root.pillH + 12) - hubPanel.headerH - 24
            onCloseRequested: root.hubOpen = false
            onTabPicked: (i) => root.hubTab = i
            onToastRequested: (text) => root.showToast(text)
        }
    }

    // ------------------------------------------------------------ command palette
    // The hotkey's card: search apps, switch windows, do maths, search the
    // web, or run one of Noch's own commands.
    Flyout {
        id: paletteFlyout
        objectName: "paletteFlyout"
        open: root.paletteOpen
        radius: 22
        baseY: root.shownY + root.pillH + 12

        width: paletteCard.implicitWidth
        height: paletteCard.implicitHeight
        x: Math.round((root.width - width) / 2)

        PaletteCard {
            id: paletteCard
            anchors.fill: parent
            onDismissed: root.closePalette()
        }
    }

    // ------------------------------------------------------------ buddy
    // Noch's little helper: a creature hanging from a rope at the top edge.
    // Click it to ask for things. Real routes (focus timer, app opening,
    // palette, clipboard, mic) and clearly-labelled demo cards (chart,
    // research, plan) are implemented in Buddy.qml + BuddyRouter.qml; the
    // result card reuses the app's glass and motion conventions.
    readonly property real buddyAnchor: cfg.buddyX >= 0
        ? Math.max(48, Math.min(width - 48, cfg.buddyX))
        : Math.max(48, Math.min(Math.round(width * 0.14),
                                Math.round((width - pillW) / 2 - (openRow.visible ? openRow.width + musicGap : 0) - 64)))

    Buddy {
        id: buddy
        visible: cfg.buddyEnabled
        s: root.s
        desktop: root.contentItem
        focusTimer: sessionTimer
        savedPlan: cfg.buddyPlan
        anchor: root.buddyAnchor
        resultPanel: buddyPanel

        onBuddyMoved: (x) => cfg.buddyX = Math.round(x)
        onToastRequested: (text) => root.showToast(text)
        onPaletteRequested: root.summonPalette()
        onClipboardRequested: root.openHub(2)
        onSettingsRequested: root.toggleSettings()
        onActivationRequested: root.requestActivate()
    }

    BuddyPanel {
        id: buddyPanel
        objectName: "buddyPanel"
        desktop: root.contentItem

        onDismissed: {
            open = false
            buddy.session = "idle"
        }
        onNewRequestRequested: buddy.openInput()
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
        target: player
        function onPlaybackError(message) { root.showToast(message) }
        function onFolderScanNotice(message) { root.showToast(message) }
    }

    // Ctrl+Alt+Space: the global summon. The switch in Settings enables or
    // disables the registration; if the combination is taken, registration
    // simply fails and everything else keeps working.
    Connections {
        target: hotkey
        function onActivated() { root.summonPalette() }
        function onRegisteredChanged() { root.hotkeyNotice() }
    }
    Binding { target: hotkey; property: "enabled"; value: cfg.hotkeyPalette }

    Connections {
        target: search
        function onCommandRequested(command) { root.runCommand(command) }
        function onToastRequested(text) { root.showToast(text) }
    }

    Connections {
        target: radios
        function onDenied() { root.showToast(qsTr("Windows blocked that switch")) }
    }

    Connections {
        target: nightLight
        function onFailed() { root.showToast(qsTr("Night light could not be switched here")) }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Pin an app to Noch")
        currentFolder: StandardPaths.writableLocation(StandardPaths.ApplicationsLocation)
        nameFilters: [qsTr("Apps and shortcuts (*.exe *.lnk *.url *.bat *.cmd)"), qsTr("All files (*)")]
        onAccepted: {
            if (dock.pinUrl(selectedFile))
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
            const n = player.addUrls(selectedFiles)
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
