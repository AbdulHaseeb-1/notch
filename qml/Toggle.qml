import QtQuick

// Pill switch: white track + black knob when on (Grok style).
Item {
    id: toggle
    property bool checked: false
    property bool enabled2: true
    signal toggled(bool checked)

    Theme { id: t }

    implicitWidth: 40
    implicitHeight: 22
    opacity: enabled2 ? 1 : 0.4

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: toggle.checked ? t.white : (hit.containsMouse ? t.press : t.border)
        Behavior on color { ColorAnimation { duration: 160 } }
    }

    Rectangle {
        id: knob
        width: parent.height - 6
        height: width
        radius: width / 2
        y: 3
        x: toggle.checked ? parent.width - width - 3 : 3
        color: toggle.checked ? t.bg : t.text
        scale: hit.pressed ? 0.86 : 1
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on scale { NumberAnimation { duration: 120 } }
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
