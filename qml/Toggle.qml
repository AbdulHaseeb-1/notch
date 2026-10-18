import QtQuick

// Pill switch: white track + black knob when on (Grok style).
Item {
    id: toggle
    property bool checked: false
    property bool enabled2: true
    signal toggled(bool checked)


    implicitWidth: 40
    implicitHeight: 22
    opacity: enabled2 ? 1 : 0.4

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: toggle.checked ? Theme.accent : (hit.containsMouse ? Theme.press : Theme.border)
        Behavior on color { ColorAnimation { duration: 160 } }
    }

    Rectangle {
        id: knob
        width: parent.height - 6
        height: width
        radius: width / 2
        y: 3
        x: toggle.checked ? parent.width - width - 3 : 3
        color: toggle.checked ? Theme.inverse : Theme.text
        scale: hit.pressed ? 0.88 : 1
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        enabled: toggle.enabled2
        cursorShape: Qt.PointingHandCursor
        onClicked: toggle.toggled(!toggle.checked)
    }
}
