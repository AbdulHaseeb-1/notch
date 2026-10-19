pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// The agent's own pointer, drawn in the overlay window that covers the
// display Buddy controls. It never touches the real cursor: it glides to each
// target on a curved, eased path, rings on a click, trails a dashed line while
// dragging and shows a little "typing" pill while keys are sent.
//
// All positions are in this item's coordinates (logical pixels relative to
// the overlay window's top-left).
Item {
    id: agentCursor

    property point target: Qt.point(width / 2, height / 2)
    property int moveMs: 320
    property int clickSerial: 0
    property bool dragging: false
    property point dragFrom: Qt.point(0, 0)
    property bool typing: false
    property bool paused: false
    property string label: qsTr("Buddy")

    // Current tip position along the path.
    property real tipX: target.x
    property real tipY: target.y

    // Path state: quadratic bezier from (fromX, fromY) through a control
    // point bowed off the straight line, to the target.
    property real fromX: tipX
    property real fromY: tipY
    property real ctrlX: tipX
    property real ctrlY: tipY
    property real toX: tipX
    property real toY: tipY
    property real t: 1

    onTChanged: {
        const u = 1 - t
        tipX = u * u * fromX + 2 * u * t * ctrlX + t * t * toX
        tipY = u * u * fromY + 2 * u * t * ctrlY + t * t * toY
    }

    onTargetChanged: {
        const dx = target.x - tipX
        const dy = target.y - tipY
        const dist = Math.sqrt(dx * dx + dy * dy)
        if (moveMs <= 0 || dist < 1 || !visible) {
            glide.stop()
            fromX = toX = ctrlX = target.x
            fromY = toY = ctrlY = target.y
            t = 1
            tipX = target.x
            tipY = target.y
            return
        }
        fromX = tipX
        fromY = tipY
        toX = target.x
        toY = target.y
        // Bow the path a little to one side, like a hand moving a mouse.
        const bow = Math.min(90, dist * 0.18)
        ctrlX = (fromX + toX) / 2 - dy / dist * bow
        ctrlY = (fromY + toY) / 2 + dx / dist * bow
        glide.duration = moveMs
        t = 0
        glide.restart()
    }

    NumberAnimation {
        id: glide
        target: agentCursor
        property: "t"
        from: 0
        to: 1
        easing.type: Easing.InOutCubic
    }

    onClickSerialChanged: ripple.restart()

    // ── drag trail ─────────────────────────────────────────────────────────
    // A dark under-stroke keeps the light dashes readable on light apps.
    Shape {
        anchors.fill: parent
        visible: agentCursor.dragging
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Qt.rgba(Theme.stone950.r, Theme.stone950.g, Theme.stone950.b, 0.55)
            strokeWidth: 4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: agentCursor.dragFrom.x
            startY: agentCursor.dragFrom.y
            PathLine { x: agentCursor.tipX; y: agentCursor.tipY }
        }
    }
    Shape {
        anchors.fill: parent
        visible: agentCursor.dragging
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.accentHover
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: agentCursor.dragFrom.x
            startY: agentCursor.dragFrom.y
            PathLine { x: agentCursor.tipX; y: agentCursor.tipY }
        }
    }
    Rectangle {
        visible: agentCursor.dragging
        x: agentCursor.dragFrom.x - 6
        y: agentCursor.dragFrom.y - 6
        width: 12
        height: 12
        radius: 6
        color: Theme.accent
        border.width: 2
        border.color: Theme.stone950
    }

    // ── click ripple ──────────────────────────────────────────────────────
    Item {
        id: rippleHost
        x: agentCursor.tipX
        y: agentCursor.tipY

        Rectangle {
            id: ring
            width: 44
            height: 44
            x: -width / 2
            y: -height / 2
            radius: width / 2
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
            border.width: 2
            border.color: Theme.accent
            opacity: 0
            scale: 0.2
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: width / 2
                color: "transparent"
                border.width: 1.5
                border.color: Qt.rgba(Theme.stone950.r, Theme.stone950.g, Theme.stone950.b, 0.55)
            }
        }
        Rectangle {
            id: dot
            width: 10
            height: 10
            x: -5
            y: -5
            radius: 5
            color: Theme.accent
            opacity: 0
        }

        ParallelAnimation {
            id: ripple
            NumberAnimation { target: ring; property: "scale"; from: 0.25; to: 1.25; duration: 460; easing.type: Easing.OutCubic }
            NumberAnimation { target: ring; property: "opacity"; from: 0.95; to: 0; duration: 520; easing.type: Easing.InQuad }
            NumberAnimation { target: dot; property: "opacity"; from: 0.9; to: 0; duration: 260 }
        }
    }

    // ── pointer + label ───────────────────────────────────────────────────
    Item {
        id: pointer
        x: agentCursor.tipX
        y: agentCursor.tipY
        scale: ripple.running && ring.scale < 0.6 ? 0.9 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }

        // Soft shadow.
        Shape {
            x: 1.5
            y: 2
            opacity: 0.45
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "black"
                strokeColor: "transparent"
                startX: 0; startY: 0
                PathLine { x: 0; y: 21 }
                PathLine { x: 5.5; y: 16 }
                PathLine { x: 9.2; y: 24.5 }
                PathLine { x: 12.6; y: 23 }
                PathLine { x: 9; y: 14.8 }
                PathLine { x: 16; y: 14.8 }
                PathLine { x: 0; y: 0 }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: Theme.stone950
                strokeColor: agentCursor.paused ? Theme.warning : Theme.accent
                strokeWidth: 1.6
                joinStyle: ShapePath.RoundJoin
                startX: 0; startY: 0
                PathLine { x: 0; y: 21 }
                PathLine { x: 5.5; y: 16 }
                PathLine { x: 9.2; y: 24.5 }
                PathLine { x: 12.6; y: 23 }
                PathLine { x: 9; y: 14.8 }
                PathLine { x: 16; y: 14.8 }
                PathLine { x: 0; y: 0 }
            }
        }

        Rectangle {
            id: tag
            x: 15
            y: 22
            height: 20
            width: tagRow.implicitWidth + 16
            radius: 10
            color: agentCursor.paused ? Theme.warning : Theme.accent
            border.width: 1.5
            border.color: Theme.stone950

            Row {
                id: tagRow
                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: agentCursor.label
                    color: Theme.inverse
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                }

                // Typing indicator: three dots that bounce in turn.
                Row {
                    id: typingDots
                    anchors.verticalCenter: parent.verticalCenter
                    visible: agentCursor.typing
                    spacing: 2.5
                    Repeater {
                        model: 3
                        Rectangle {
                            id: typeDot
                            required property int index
                            width: 4
                            height: 4
                            radius: 2
                            color: Theme.inverse
                            SequentialAnimation on opacity {
                                running: typingDots.visible
                                loops: Animation.Infinite
                                PauseAnimation { duration: typeDot.index * 140 }
                                NumberAnimation { to: 1; duration: 220 }
                                NumberAnimation { to: 0.35; duration: 260 }
                                PauseAnimation { duration: (2 - typeDot.index) * 140 }
                            }
                        }
                    }
                }
            }
        }
    }
}
