import QtQuick

// The buddy's result card: one repositionable, resizable glass panel that
// hosts whatever the current task produced (chart, research digest, day plan,
// capability list). Loading / error / sample-data labelling live here so the
// routing logic in BuddyRouter stays pure.
//
// Position + size: `dragX/dragY` are owned by the panel (the drag handler
// writes them, clamped); the entrance spring only offsets the drawn y, so
// dragging never fights an animation binding. Buddy seeds the position and
// calls clampIntoView() when the desktop changes.
Item {
    id: panel

    property bool open: false
    property string kind: "help"          // chart | research | tasks | help
    property string title: ""
    property string phase: "ready"        // ready | loading | error
    property string errorMessage: ""
    property var payload: null            // task-specific data from BuddyRouter
    property bool simulated: true

    // Real desktop bounds for clamping (usually root.contentItem).
    property var desktop: null

    signal dismissed()
    signal retryRequested()
    signal newRequestRequested()
    signal planEdited(var items)
    signal capability(string id)

    Theme { id: t }

    readonly property real pad: 14
    readonly property real headerH: 44
    readonly property real footerH: 40
    readonly property real width_: 420
    property real dragX: 0
    property real dragY: 0

    // Body height: defaults per kind, user-resizable with the bottom grip.
    property real bodyH: 336
    property bool userResized: false

    implicitWidth: width_
    width: width_
    height: headerH + 1 + bodyH + 1 + footerH
    x: dragX
    y: dragY - 10 * (1 - pc)
    visible: opacity > 0.005

    // Critically-damped entrance, matching the notch flyouts.
    property real p: open ? 1 : 0
    Behavior on p {
        NumberAnimation {
            duration: panel.open ? 210 : 150
            easing.type: panel.open ? Easing.OutQuint : Easing.InCubic
        }
    }
    readonly property real pc: Math.max(0, Math.min(1, p))
    opacity: Math.min(1, pc * 1.8)
    scale: 0.95 + 0.05 * pc
    transformOrigin: Item.Top

    onKindChanged: resetBody()
    onOpenChanged: {
        if (!open)
            userResized = false
        else
            resetBody()
    }

    function resetBody() {
        if (userResized)
            return
        const avail = maxBody()
        bodyH = kind === "chart" ? Math.min(avail, 344)
              : kind === "tasks" ? Math.min(avail, 284)
              : kind === "help"  ? Math.min(avail, 326)
              : Math.min(avail, 336)
    }

    function maxBody() {
        return desktop
            ? Math.max(220, desktop.height - Math.max(10, dragY) - headerH - footerH - 18)
            : 480
    }

    // Keep the card inside the desktop rect (also called by Buddy).
    function clampIntoView() {
        if (!desktop)
            return
        const lo = 10
        dragX = Math.round(Math.max(lo, Math.min(desktop.width - width_ - lo, dragX)))
        dragY = Math.round(Math.max(lo, Math.min(desktop.height - height - lo, dragY)))
    }

    // ---- payload helpers (the content cards read through these) ----

    function profit(v) {
        const n = Number(v)
        if (n >= 1000)
            return n.toLocaleString(Qt.locale("en-US"), 'f', 0)
        if (n >= 100)
            return n.toFixed(0)
        return n.toFixed(2)
    }

    function chartSummaryText() {
        if (!payload || !payload.bias)
            return ""
        const bias = payload.bias === "uptrend" ? "trending up"
                   : (payload.bias === "downtrend" ? "trending down" : "moving sideways")
        const strong = payload.strength >= 0.6 ? "with conviction"
                     : (payload.strength >= 0.4 ? "moderately" : "weakly")
        const r = payload.resistance && payload.resistance.length
                  ? profit(payload.resistance[0]) : "no nearby resistance"
        const s = payload.support && payload.support.length
                  ? profit(payload.support[0]) : "no nearby support"
        const dir = payload.changePct >= 0 ? "up" : "down"
        // Parenthesised: .arg() would bind to the last literal otherwise.
        return ("The sample series closes %1 %2 over the window. Bias says %3, %4. "
                + "Would-be watch: %5 above for sellers, %6 below for buyers. "
                + "Both levels and trend are read from the local fixture — no feed is wired up.")
            .arg(symbolText()).arg(dir).arg(bias).arg(strong).arg(r).arg(s)
    }

    function symbolText() {
        return payload && payload.symbol ? payload.symbol : ""
    }

    Glass {
        anchors.fill: parent
        radius: 20
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- header -------------------------------------------------------
        Item {
            width: parent.width
            height: panel.headerH

            MouseArea {
                id: dragHit
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor
                property real gx: 0
                property real gy: 0
                property real sx: 0
                property real sy: 0
                onPressed: (mouse) => {
                    const g = panel.desktop
                              ? dragHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                              : Qt.point(mouse.x, mouse.y)
                    gx = g.x
                    gy = g.y
                    sx = panel.dragX
                    sy = panel.dragY
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const g = panel.desktop
                              ? dragHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                              : Qt.point(mouse.x, mouse.y)
                    panel.dragX = sx + (g.x - gx)
                    panel.dragY = sy + (g.y - gy)
                    panel.clampIntoView()
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 5
                width: 28
                height: 3
                radius: 1.5
                color: t.borderStrong
            }

            Glyph {
                id: titleIcon
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.verticalCenter: parent.verticalCenter
                name: panel.kind === "chart" ? "chart"
                    : (panel.kind === "research" ? "doc"
                      : (panel.kind === "tasks" ? "tasks" : "spark"))
                size: 16
                stroke: 2
                color: t.accent
            }

            Text {
                anchors.left: titleIcon.right
                anchors.leftMargin: 8
                anchors.right: chip.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: panel.title
                color: t.ink
                font.family: t.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            // State chip: Working… / Ready / Failed.
            Rectangle {
                id: chip
                anchors.right: closeBtn.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                height: 22
                radius: 11
                color: panel.phase === "error"
                       ? t.dangerSoft
                       : (panel.phase === "loading"
                          ? t.tile
                          : Qt.rgba(t.success.r, t.success.g, t.success.b, 0.16))
                border.width: 1
                border.color: panel.phase === "error"
                              ? t.danger
                              : (panel.phase === "loading" ? t.border
                                 : Qt.rgba(t.success.r, t.success.g, t.success.b, 0.45))

                Text {
                    anchors.centerIn: parent
                    text: panel.phase === "error" ? qsTr("Failed")
                        : (panel.phase === "loading" ? qsTr("Working…") : qsTr("Ready"))
                    color: panel.phase === "error" ? t.danger
                        : (panel.phase === "loading" ? t.textDim : t.success)
                    font.family: t.font
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }

            Item {
                id: closeBtn
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22

                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 13
                    stroke: 2
                    color: closeHit.containsMouse ? t.ink : t.textFaint
                }
                MouseArea {
                    id: closeHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.dismissed()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: t.borderSoft
            }
        }

        // ---- body -----------------------------------------------------------
        Item {
            width: parent.width
            height: panel.bodyH
            clip: true

            // Loading cover.
            Item {
                anchors.fill: parent
                opacity: panel.phase === "loading" ? 1 : 0
                visible: opacity > 0.01
                enabled: panel.phase === "loading"

                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: panel.kind === "chart" ? qsTr("Pulling up the demo chart…")
                            : (panel.kind === "research" ? qsTr("Gathering sample notes…")
                              : (panel.kind === "tasks" ? qsTr("Sketching a plan…")
                                : qsTr("Thinking…")))
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 12
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4
                        Repeater {
                            model: 3
                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: t.accent
                                SequentialAnimation on opacity {
                                    running: panel.phase === "loading"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.2; duration: 370 }
                                    NumberAnimation { to: 1; duration: 370 }
                                }
                            }
                        }
                    }
                }
            }

            // Error cover.
            Item {
                anchors.fill: parent
                opacity: panel.phase === "error" ? 1 : 0
                visible: opacity > 0.01
                enabled: panel.phase === "error"

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 60
                        text: panel.errorMessage
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 96
                        height: 28
                        radius: 14
                        color: retryBtn.pressed ? t.press
                             : (retryBtn.containsMouse ? t.tileActive : t.tile)
                        border.width: 1
                        border.color: t.border

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Try again")
                            color: t.ink
                            font.family: t.font
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: retryBtn
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.retryRequested()
                        }
                    }
                }
            }

            BuddyChartCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "chart"
            }
            BuddyResearchCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "research"
            }
            BuddyTasksCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "tasks"
            }
            BuddyHelpCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "help"
            }
        }

        Rectangle { width: parent.width; height: 1; color: t.borderSoft }

        // ---- footer -----------------------------------------------------------
        Item {
            width: parent.width
            height: panel.footerH

            // Say sample-vs-real out loud, whichever it is.
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.verticalCenter: parent.verticalCenter
                width: sampleLabel.implicitWidth + 16
                height: 22
                radius: 11
                color: panel.simulated
                       ? t.tile
                       : Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.14)
                border.width: 1
                border.color: panel.simulated
                              ? t.border
                              : Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.4)

                Text {
                    id: sampleLabel
                    anchors.centerIn: parent
                    text: panel.simulated ? qsTr("Sample content") : qsTr("Handled by Noch")
                    color: panel.simulated ? t.textDim : t.accent
                    font.family: t.font
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                anchors.verticalCenter: parent.verticalCenter
                width: 122
                height: 26
                radius: 13
                color: newReqHit.pressed ? t.press
                      : (newReqHit.containsMouse ? t.tileActive : t.tile)
                border.width: 1
                border.color: t.border

                Text {
                    anchors.centerIn: parent
                    text: qsTr("New request")
                    color: t.ink
                    font.family: t.font
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: newReqHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.newRequestRequested()
                }
            }
        }
    }

    // Bottom grip: drag to change the body height, clamped into the desktop.
    // Narrowed to the middle so it never covers the footer buttons.
    MouseArea {
        id: resizeHit
        visible: panel.phase === "ready"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        width: Math.max(60, panel.width_ - 300)
        height: 12
        cursorShape: Qt.SizeVerCursor
        property real gy: 0
        property real startBody: 0
        onPressed: (mouse) => {
            const g = panel.desktop
                      ? resizeHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                      : Qt.point(mouse.x, mouse.y)
            gy = g.y
            startBody = panel.bodyH
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const g = panel.desktop
                      ? resizeHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                      : Qt.point(mouse.x, mouse.y)
            panel.userResized = true
            panel.bodyH = Math.max(200, Math.min(panel.maxBody(), startBody + (g.y - gy)))
            panel.clampIntoView()
        }
    }
}
