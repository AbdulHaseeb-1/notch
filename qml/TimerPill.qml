import QtQuick
import QtQuick.Shapes

// Live activity for the focus timer: a progress ring and the time left, in a
// pill beside the music pill. It pops in with a spring when a session starts
// and pulses when one ends. Click to open the timer.
Item {
    id: pill

    property real s: 1
    property real appear: 1
    property var timer          // FocusTimer
    property bool highlight: false

    signal clicked()
    signal hintRequested(string text)
    signal hintCleared()

    Theme { id: t }

    readonly property bool shown: timer && timer.active
    readonly property bool hovered: hover.hovered
    readonly property real pillH: Math.round(50 * s)
    readonly property real ringSize: Math.round(24 * s)

    // The pill expands and contracts smoothly.
    property real grow: shown ? 1 : 0
    Behavior on grow {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }
    width: Math.max(0, grow) * (row.implicitWidth + Math.round(26 * s))
    height: pillH
    opacity: shown ? appear : 0
    visible: opacity > 0.01 && width > 1
    Behavior on opacity { NumberAnimation { duration: 180 } }

    // Smooth the ring between the timer's 200ms ticks.
    property real shownRatio: timer ? timer.ratio : 0
    Behavior on shownRatio { NumberAnimation { duration: 220; easing.type: Easing.Linear } }

    // Finished: the whole pill breathes until it is dismissed.
    property real pulse: 0
    SequentialAnimation on pulse {
        running: pill.timer && pill.timer.finished && pill.visible
        loops: Animation.Infinite
        onRunningChanged: if (!running) pill.pulse = 0
        NumberAnimation { to: 1; duration: 520; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0; duration: 520; easing.type: Easing.InOutSine }
    }
    scale: (hover.hovered ? 1.03 : 1) * (1 + 0.04 * pulse) * (tap.pressed ? 0.96 : 1)
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Glass {
        anchors.fill: parent
        radius: height / 2
        edge: pill.timer && pill.timer.finished ? Qt.rgba(t.success.r, t.success.g, t.success.b, 0.35 + 0.5 * pill.pulse)
                                                : "transparent"
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Math.round(8 * pill.s)

        Item {
            width: pill.ringSize
            height: width
            anchors.verticalCenter: parent.verticalCenter

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: Math.max(2.2, 2.6 * pill.s)
                    strokeColor: t.track
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: pill.ringSize / 2; centerY: pill.ringSize / 2
                        radiusX: pill.ringSize / 2 - 2; radiusY: pill.ringSize / 2 - 2
                        startAngle: 0; sweepAngle: 360
                    }
                }
                ShapePath {
                    strokeWidth: Math.max(2.2, 2.6 * pill.s)
                    strokeColor: pill.timer && pill.timer.finished ? t.success : t.ink
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: pill.ringSize / 2; centerY: pill.ringSize / 2
                        radiusX: pill.ringSize / 2 - 2; radiusY: pill.ringSize / 2 - 2
                        startAngle: -90
                        sweepAngle: pill.timer && pill.timer.finished ? 360 : Math.max(0.01, 360 * pill.shownRatio)
                    }
                }
            }
            // Paused marker.
            Glyph {
                visible: pill.timer && !pill.timer.running && !pill.timer.finished
                anchors.centerIn: parent
                name: "pause"
                size: Math.round(10 * pill.s)
                color: t.ink
            }
            Glyph {
                visible: pill.timer && pill.timer.finished
                anchors.centerIn: parent
                name: "check"
                size: Math.round(12 * pill.s)
                stroke: 2.6
                color: t.success
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.timer ? (pill.timer.finished ? qsTr("Done") : pill.timer.label) : ""
            color: pill.timer && pill.timer.finished ? t.success : t.ink
            font.family: t.font
            font.pixelSize: Math.round(14 * pill.s)
            font.weight: Font.Bold
            font.features: ({ "tnum": 1 })
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hovered)
                pill.hintRequested(pill.timer && pill.timer.finished ? qsTr("Session finished")
                                   : (pill.timer && pill.timer.running ? qsTr("Focus timer") : qsTr("Focus timer (paused)")))
            else
                pill.hintCleared()
        }
    }

    TapHandler {
        id: tap
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: pill.clicked()
    }
}
