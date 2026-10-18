import QtQuick

// The "⋯" button at the end of the notch that opens settings.
Item {
    id: more
    property real s: 1
    property bool active: false
    signal clicked()

    Theme { id: t }

    readonly property bool hovered: hit.containsMouse

    implicitWidth: Math.round(40 * s)
    implicitHeight: Math.round(40 * s)

    Rectangle {
        anchors.centerIn: parent
        width: Math.round(34 * more.s)
        height: width
        radius: width / 2
        color: more.active ? t.accent : (hit.pressed ? t.press : t.raised)
        opacity: more.active || hit.containsMouse ? 1 : 0
        scale: hit.pressed ? 0.9 : 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on scale { SpringAnimation { spring: 8; damping: 0.35; epsilon: 0.001 } }
    }

    Row {
        anchors.centerIn: parent
        spacing: Math.max(2, Math.round(3 * more.s))
        rotation: more.active ? 90 : 0
        Behavior on rotation { SpringAnimation { spring: 4; damping: 0.28; epsilon: 0.1 } }
        Repeater {
            model: 3
            Rectangle {
                width: Math.max(3, Math.round(4 * more.s))
                height: width
                radius: width / 2
                color: more.active ? t.inverse : (hit.containsMouse ? t.ink : t.text)
            }
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: more.clicked()
    }
}
