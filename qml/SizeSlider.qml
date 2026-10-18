import QtQuick

// Thin X-style slider: grey track, white fill, white knob that grows on hover.
Item {
    id: slider
    property real from: 0
    property real to: 1
    property real value: 0
    signal moved(real value)

    Theme { id: t }

    implicitWidth: 200
    implicitHeight: 24

    readonly property real targetRatio: Math.max(0, Math.min(1, (value - from) / Math.max(0.0001, to - from)))
    property real ratio: targetRatio
    Behavior on ratio {
        enabled: !slider.dragging
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }
    readonly property bool active: hit.containsMouse || hit.pressed
    readonly property bool dragging: hit.pressed

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 2
        color: t.border
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: knob.x + knob.width / 2
        height: 4
        radius: 2
        color: t.accent
    }

    Rectangle {
        id: knob
        width: slider.active ? 16 : 13
        height: width
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        x: slider.ratio * (slider.width - width)
        color: t.accent
        border.width: hit.pressed ? 3 : 0
        border.color: t.hairline
        Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        preventStealing: true

        function update(mx) {
            const r = Math.max(0, Math.min(1, mx / slider.width))
            slider.moved(slider.from + r * (slider.to - slider.from))
        }
        onPressed: (mouse) => update(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) update(mouse.x) }
    }
}
