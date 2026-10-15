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
    height: Math.min(Screen.height, 960)

    Theme { id: t }

    Settings {
        id: cfg
        category: "ui"
        property real sizeScale: 1.0
        property bool autoHide: false
        property int hideDelay: 1200
        property bool monoIcons: true
        property bool showClock: true
        property bool showStats: false
        property int focusMinutes: 25
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
    readonly property real peekRatio: 0.05          // share of the notch left on screen when hidden
    readonly property real pillW: Math.max(Math.round(150 * s), dockRow.implicitWidth + 2 * padX)
    readonly property real shownY: topGap
    readonly property real hiddenY: -pillH * (1 - peekRatio)

    function setScale(v) {
        cfg.sizeScale = Math.round(Math.max(0.7, Math.min(1.6, v)) * 100) / 100
        hudTimer.restart()
    }

    // ---------------------------------------------------------- reveal logic
    property bool panelOpen: false
    property bool musicOpen: false
    property bool hubOpen: false
    property int hubTab: 0
    property int menuIndex: -1
    property bool resizing: false
    property bool launchHold: false
    property bool introHold: true
    property bool revealed: true

    readonly property bool pointerInside: pillHover.hovered || peekZone.containsMouse
                                          || grip.containsMouse || panelItem.hovered
                                          || menu.hovered || musicFlyout.hovered
                                          || hubFlyout.hovered || musicPillHover.hovered
                                          || openRow.hovered || previewCard.hovered || timerPill.hovered
    readonly property bool dragging: peekDrop.containsDrag || pillDrop.containsDrag
                                     || musicDrop.containsDrag
    readonly property bool holdOpen: !cfg.autoHide || panelOpen || musicOpen || hubOpen
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

    // 0 = tucked away, 1 = fully shown (overshoots a little on the way down).
    // A spring rather than a fixed curve: it drops in with a lively overshoot
    // and tucks away firmly, and it picks up from wherever it was if the
    // pointer changes its mind part-way.
    property real progress: revealed ? 1 : 0
    Behavior on progress {
        SpringAnimation {
            spring: root.revealed ? 2.7 : 5.2
            damping: root.revealed ? 0.2 : 0.72
            epsilon: root.revealed ? 0.0006 : 0.004
        }
    }
    readonly property bool fullyHidden: !revealed && progress <= 0.001

    function clamp01(v) { return Math.max(0, Math.min(1, v)) }
    function itemAppear(i) { return clamp01((progress - 0.18 - i * 0.045) / 0.45) }

    // ------------------------------------------------------------ tooltip
    property int hoverIndex: -1
    property real tipX: 0
    property bool tipReady: false
    property string toast: ""
    property string hoverHint: ""
    readonly property string tipText: toast.length > 0 ? toast
        : (hoverIndex >= 0 && hoverIndex < apps.length ? apps[hoverIndex].name : hoverHint)
    readonly property bool tipVisible: revealed && !resizing && !panelOpen && !musicOpen
                                       && !hubOpen && menuIndex < 0
                                       && tipText.length > 0 && (toast.length > 0 || tipReady)

    Timer { id: tipTimer; interval: 380; onTriggered: root.tipReady = true }
    Timer { id: tipGrace; interval: 160; onTriggered: if (root.hoverIndex < 0) root.tipReady = false }
    Timer { id: toastTimer; interval: 1300; onTriggered: root.toast = "" }
    Timer { id: hudTimer; interval: 900 }
    readonly property bool hudVisible: resizing || hudTimer.running

    // ------------------------------------------------------------ data
    readonly property var apps: dock.apps
    readonly property var running: dock.running
    readonly property var openList: openApps.apps

    function launchApp(i) {
        if (i < 0 || i >= apps.length)
            return
        menuIndex = -1
        panelOpen = false
        musicOpen = false
        hubOpen = false
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
        menuX = centerX
        menuIndex = (menuIndex === i) ? -1 : i
    }

    function closeAll() {
        panelOpen = false
        musicOpen = false
        hubOpen = false
        menuIndex = -1
    }

    function toggleMusic() {
        menuIndex = -1
        panelOpen = false
        hubOpen = false
        musicOpen = !musicOpen
    }

    // Opens the hub on a page (or closes it if that page is already showing).
    // tab -1 means "wherever it was last".
    function toggleHub(tab) {
        menuIndex = -1
        panelOpen = false
        musicOpen = false
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
        hubTab = tab
        hubOpen = true
    }

    function toggleSettings() {
        menuIndex = -1
        musicOpen = false
        hubOpen = false
        panelOpen = !panelOpen
    }

    function showToast(text) {
        toast = text
        toastTimer.restart()
    }

    onActiveChanged: if (!active && !fileDialog.visible) closeAll()

    Shortcut { sequence: "Esc"; onActivated: root.closeAll() }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.panelOpen = !root.panelOpen }

    // ------------------------------------------------------------ mask
    // Only the visible parts of the window take the mouse; everything else
    // clicks through to the desktop.
    property real menuX: 0
    readonly property bool panelVisible: panelItem.shown
    readonly property bool menuVisible: menu.shown
    readonly property bool musicVisible: musicFlyout.shown
    readonly property bool hubVisible: hubFlyout.shown
    // Flyout sizes for the mask are the sizes they are heading for, not the
    // animated ones, so the mask is not rebuilt on every frame of an animation.
    readonly property real panelTargetH: settings.targetHeight
    readonly property real hubTargetH: hubPanel.targetHeight
    readonly property var maskKey: [fullyHidden, Math.round(pillW), Math.round(pillH), width,
                                    Math.round(musicDock.width), Math.round(timerPill.width),
                                    panelVisible, Math.round(panelTargetH), menuVisible,
                                    Math.round(menu.x), Math.round(menu.height),
                                    musicVisible, Math.round(musicFlyout.x), Math.round(musicFlyout.height),
                                    hubVisible, Math.round(hubTargetH),
                                    tipVisible, hudVisible,
                                    openRow.visible, Math.round(openRow.x),
                                    previewShown, Math.round(previewCard.x),
                                    Math.round(previewCard.width), Math.round(previewCard.height)]
    onMaskKeyChanged: Qt.callLater(updateMask)

    function updateMask() {
        const r = []
        if (fullyHidden) {
            r.push({ x: peekZone.x, y: 0, width: peekZone.width, height: peekZone.height, radius: 0 })
        } else {
            const m = 22
            const notchLeft = (width - pillW * 1.04) / 2 - m
            const left = openRow.visible ? Math.min(notchLeft, openRow.x - m) : notchLeft
            const rightEdge = timerPill.visible ? timerPill.x + timerPill.width
                                                : (width + pillW) / 2 + musicGap + musicDock.width
            const right = Math.max(rightEdge, (width + pillW) / 2 + musicGap + musicDock.width) + m
            const below = (tipVisible || hudVisible) ? 56 : m
            r.push({ x: left, y: 0, width: right - left, height: shownY + pillH + below, radius: 30 })
            if (panelVisible) {
                r.push({ x: panelItem.x - m, y: panelItem.baseY - 16,
                         width: panelItem.width + 2 * m, height: panelTargetH + m + 24, radius: 34 })
            }
            if (menuVisible) {
                r.push({ x: menu.x - m, y: menu.baseY - 16,
                         width: menu.width + 2 * m, height: menu.height + m + 24, radius: 28 })
            }
            if (musicVisible) {
                r.push({ x: musicFlyout.x - m, y: musicFlyout.baseY - 16,
                         width: musicFlyout.width + 2 * m, height: musicFlyout.height + m + 24, radius: 34 })
            }
            if (hubVisible) {
                r.push({ x: hubFlyout.x - m, y: hubFlyout.baseY - 16,
                         width: hubFlyout.width + 2 * m, height: hubTargetH + m + 24, radius: 34 })
            }
            if (previewShown) {
                r.push({ x: previewCard.x - m, y: previewCard.y - 16,
                         width: previewCard.width + 2 * m, height: previewCard.height + m + 24, radius: 34 })
            }
        }
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
            g.push({ x: pill.x, y: pill.y, width: pill.width, height: pill.height, radius: pill.height / 2 })
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
            if (previewShown) g.push(glassShape(previewCard, 22))
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

    // ------------------------------------------------------------ peek zone
    // While tucked away, a thin strip at the screen edge catches the pointer
    // (and file drags) and pulls the notch back down.
    MouseArea {
        id: peekZone
        x: Math.round((root.width - root.pillW * 0.82) / 2)
        y: 0
        width: Math.round(root.pillW * 0.82)
        height: Math.max(6, Math.ceil(root.pillH * root.peekRatio) + 3)
        hoverEnabled: true
        onClicked: root.revealed = true

        DropArea {
            id: peekDrop
            anchors.fill: parent
        }
    }

    // ------------------------------------------------------------ notch
    Item {
        id: pill
        width: root.pillW * (0.82 + 0.18 * root.progress)
        height: root.pillH
        x: Math.round((root.width - width) / 2)
        y: root.hiddenY + (root.shownY - root.hiddenY) * root.progress

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
            edge: root.dragging ? t.ink
                : (root.fullyHidden && peekZone.containsMouse ? t.textDim : "transparent")
        }

        // Grab bar shown at the bottom edge while tucked away.
        Rectangle {
            id: peekBar
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            height: 2
            radius: 1
            width: peekZone.containsMouse ? 64 * root.s : 36 * root.s
            color: peekZone.containsMouse ? t.ink : t.textDim
            opacity: root.clamp01(1 - root.progress * 3)
            Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 200 } }
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
            clip: true

            Row {
                id: dockRow
                anchors.centerIn: parent
                height: parent.height

                // Apps (Left side of dock, matching design mockup)
                Item {
                    id: appsArea
                    width: appsRow.width
                    height: parent.height

                    HoverHandler { id: appsHover }

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
                                    if (hovered) {
                                        root.hoverIndex = index
                                        root.tipX = dockItem.mapToItem(root.contentItem, width / 2, 0).x
                                        if (!root.tipReady)
                                            tipTimer.restart()
                                    } else if (root.hoverIndex === index) {
                                        root.hoverIndex = -1
                                        tipTimer.stop()
                                        tipGrace.restart()
                                    }
                                }
                                onActivated: (i) => root.launchApp(i)
                                onContextRequested: (i) => root.openMenu(
                                    i, dockItem.mapToItem(root.contentItem, width / 2, 0).x)
                            }
                        }

                        // Drop target placeholder.
                        Item {
                            width: root.dragging ? root.cellW : 0
                            height: parent.height
                            clip: true
                            Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutBack } }

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

                        Text {
                            visible: root.apps.length === 0 && !root.dragging
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: Math.round(8 * root.s)
                            rightPadding: Math.round(8 * root.s)
                            text: qsTr("Drop apps or songs")
                            color: t.textDim
                            font.family: t.font
                            font.pixelSize: Math.round(13 * root.s)
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Item { width: Math.round(14 * root.s); height: 1 }

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
        }
        drop.accept(Qt.CopyAction)
    }

    // ------------------------------------------------------------ music pill
    // Separate pill to the right of the notch: [♫] or [ |◀ ▶ ▶| ].
    Item {
        id: musicDock
        width: nowPlaying.width
        height: root.pillH
        x: Math.round(pill.x + pill.width + root.musicGap)
        y: pill.y
        scale: 0.7 + 0.3 * root.clamp01(root.progress)

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
        x: Math.round(musicDock.x + musicDock.width + root.musicGap)
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
                                         && !panelOpen && !musicOpen && !hubOpen && menuIndex < 0

    function openPreview(exe, centerX) {
        const windows = openApps.windowsFor(exe)
        if (windows.length === 0) {
            previewOpen = false
            return
        }
        const program = openList.filter(app => app.exe === exe)[0]
        previewExe = exe
        previewName = program ? program.name : exe
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
        interval: 280
        onTriggered: if (!openRow.hovered && !previewCard.hovered) root.previewOpen = false
    }

    OpenApps {
        id: openRow
        x: Math.round(pill.x - root.musicGap - width)
        y: pill.y
        s: root.s
        mono: cfg.monoIcons
        apps: root.openList
        appear: root.clamp01(root.progress * 1.4 - 0.2)
        scale: 0.7 + 0.3 * root.clamp01(root.progress)

        onActivated: (exe) => {
            root.previewOpen = false
            openApps.activateApp(exe)
        }
        onPreviewRequested: (exe, centerX) => {
            previewHideTimer.stop()
            if (root.previewOpen) {
                if (root.previewExe !== exe)
                    root.openPreview(exe, centerX)
            } else {
                previewShowTimer.exe = exe
                previewShowTimer.centerX = centerX
                previewShowTimer.restart()
            }
        }
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

        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

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
        x: Math.round((root.width - width) / 2)
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
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }

    // ------------------------------------------------------------ tooltip / toast / size HUD
    Rectangle {
        id: tip
        readonly property bool showHud: root.hudVisible
        readonly property string label: showHud ? Math.round(cfg.sizeScale * 100) + "%" : root.tipText

        width: tipLabel.implicitWidth + 20
        height: 26
        radius: 13
        color: showHud ? t.surface : t.ink
        border.width: showHud ? 1 : 0
        border.color: t.borderStrong
        x: Math.round((showHud || root.toast.length > 0 ? root.width / 2 : root.tipX) - width / 2)
        y: Math.round(root.shownY + root.pillH + 12 + (shown ? 0 : -6))
        opacity: shown ? 1 : 0
        scale: shown ? 1 : 0.9
        readonly property bool shown: showHud || root.tipVisible

        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on scale { SpringAnimation { spring: 6; damping: 0.34; epsilon: 0.0005 } }
        Behavior on y { SpringAnimation { spring: 6; damping: 0.5; epsilon: 0.05 } }
        Behavior on x { enabled: tip.opacity > 0.5; SpringAnimation { spring: 7; damping: 0.6; epsilon: 0.1 } }

        Text {
            id: tipLabel
            anchors.centerIn: parent
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
        property int shownIndex: -1
        property bool renaming: false
        readonly property var app: shownIndex >= 0 && shownIndex < root.apps.length ? root.apps[shownIndex] : null

        onOpenChanged: {
            if (open) {
                shownIndex = root.menuIndex
                renaming = false
            }
        }
        Connections {
            target: root
            function onMenuIndexChanged() {
                if (root.menuIndex >= 0) {
                    menu.shownIndex = root.menuIndex
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
                                dock.rename(menu.shownIndex, text)
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
                onTriggered: root.launchApp(menu.shownIndex)
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
                onTriggered: { dock.revealInFolder(menu.shownIndex); root.menuIndex = -1 }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Move left")
                icon: "left"
                visible: menu.shownIndex > 0
                onTriggered: { dock.move(menu.shownIndex, menu.shownIndex - 1); root.menuIndex = -1 }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Move right")
                icon: "right"
                visible: menu.shownIndex >= 0 && menu.shownIndex < root.apps.length - 1
                onTriggered: { dock.move(menu.shownIndex, menu.shownIndex + 1); root.menuIndex = -1 }
            }
            Item { width: 1; height: 4 }
            Rectangle { width: parent.width; height: 1; color: t.borderSoft }
            Item { width: 1; height: 4 }
            MenuRow {
                width: parent.width
                label: qsTr("Unpin")
                icon: "unpin"
                danger: true
                onTriggered: { const i = menu.shownIndex; root.menuIndex = -1; dock.unpin(i) }
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
            maxBodyHeight: root.height - (root.shownY + root.pillH + 12) - 56 - 44 - 1 - 50 - 20

            onCloseRequested: root.panelOpen = false
            onAddRequested: fileDialog.open()
            onResetRequested: dock.resetToDefaults()
            onQuitRequested: dock.quit()
            onUnpinRequested: (i) => dock.unpin(i)
            onMoveRequested: (from, to) => dock.move(from, to)
            onStartupToggled: (on) => dock.launchAtStartup = on
            onBlurToggled: (on) => glassBackdrop.enabled = on
            lightTheme: appearance.light
            onThemeToggled: (on) => appearance.light = on
            onLaunchRequested: (i) => root.launchApp(i)
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
            onCloseRequested: root.hubOpen = false
            onTabPicked: (i) => root.hubTab = i
            onToastRequested: (text) => root.showToast(text)
        }
    }

    Connections {
        target: player
        function onPlaybackError(message) { root.showToast(message) }
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
