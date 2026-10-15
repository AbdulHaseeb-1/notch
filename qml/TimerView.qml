import QtQuick
import QtQuick.Shapes

// Focus timer page of the hub: a large ring with the time inside, length
// presets underneath, and reset / start-pause controls.
Item {
    id: view

    property var timer          // FocusTimer
    signal toastRequested(string text)

    Theme { id: t }

    readonly property bool running: timer.running
    readonly property bool finished: timer.finished
    readonly property real ringSize: 168

    implicitWidth: 348
    implicitHeight: col.implicitHeight

    // Ring progress, smoothed between the timer's ticks.
    property real shownRatio: timer.ratio
    Behavior on shownRatio { NumberAnimation { duration: 220; easing.type: Easing.Linear } }

    component RoundButton: Item {
        id: rb
        property string icon
        property real glyphSize: 18
        property bool primary: false
        property bool enabled2: true
        signal clicked()

        width: primary ? 64 : 44
        height: width
        opacity: enabled2 ? 1 : 0.35
        scale: rbHit.pressed ? 0.9 : (rbHit.containsMouse ? 1.06 : 1)
        Behavior on scale { SpringAnimation { spring: 6; damping: 0.32; epsilon: 0.0005 } }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: rb.primary ? t.ink : (rbHit.containsMouse ? t.hover : t.raised)
            border.width: rb.primary ? 0 : 1
            border.color: t.borderSoft
            Behavior on color { ColorAnimation { duration: 140 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: rb.icon
            size: rb.glyphSize
            stroke: 2
            color: rb.primary ? t.inverse : t.text
        }
        MouseArea {
            id: rbHit
            anchors.fill: parent
            hoverEnabled: true
            enabled: rb.enabled2
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.clicked()
        }
    }

    Column {
        id: col
        width: parent.width
        topPadding: 20
        bottomPadding: 22
        spacing: 20

        // ---- ring
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: view.ringSize
            height: width

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: 8
                    strokeColor: t.track
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: view.ringSize / 2; centerY: view.ringSize / 2
                        radiusX: view.ringSize / 2 - 6; radiusY: view.ringSize / 2 - 6
                        startAngle: 0; sweepAngle: 360
                    }
                }
                ShapePath {
                    strokeWidth: 8
                    strokeColor: view.finished ? t.success : t.ink
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    Behavior on strokeColor { ColorAnimation { duration: 240 } }
                    PathAngleArc {
                        centerX: view.ringSize / 2; centerY: view.ringSize / 2
                        radiusX: view.ringSize / 2 - 6; radiusY: view.ringSize / 2 - 6
                        startAngle: -90
                        sweepAngle: view.finished ? 360 : Math.max(0.01, 360 * view.shownRatio)
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: view.timer.label
                    color: view.finished ? t.success : t.ink
                    font.family: t.font
                    font.pixelSize: 38
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: view.finished ? qsTr("Session finished")
                        : (view.running ? qsTr("Focusing") : (view.timer.active ? qsTr("Paused") : qsTr("Ready")))
                    color: t.textDim
                    font.family: t.font
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
        }

        // ---- controls
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18

            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "reset"
                enabled2: view.timer.active
                onClicked: view.timer.reset()
            }
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                primary: true
                icon: view.running ? "pause" : "play"
                glyphSize: 24
                onClicked: view.timer.toggle()
            }
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                icon: "plus"
                onClicked: {
                    view.timer.setMinutes(view.timer.minutes + 5)
                    view.toastRequested(qsTr("%1 min").arg(view.timer.minutes))
                }
            }
        }

        // ---- length presets
        Segmented {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 40
            options: [
                { label: "5", value: 5 },
                { label: "15", value: 15 },
                { label: "25", value: 25 },
                { label: "45", value: 45 },
                { label: "60", value: 60 }
            ]
            value: view.timer.minutes
            onPicked: (v) => view.timer.setMinutes(v)
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("minutes  ·  keeps running when this panel is closed")
            color: t.textFaint
            font.family: t.font
            font.pixelSize: 11
        }
    }
}
