pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: sb
    property bool active: false
    property string tip: qsTr("Send request")
    signal clicked()

    width: 30
    height: 30
    scale: sendHit.pressed ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: sendHit.pressed || sendHit.containsMouse ? Theme.accentHover
             : sb.active ? Theme.accent : Qt.darker(Theme.field, 1.18)
        antialiasing: true
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Glyph {
        anchors.centerIn: parent
        name: "send"
        size: 15
        color: sb.active ? Theme.inverse : Theme.textDim
    }

    MouseArea {
        id: sendHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: sb.tip
        onClicked: sb.clicked()
    }
}
