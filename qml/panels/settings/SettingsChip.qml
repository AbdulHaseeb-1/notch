pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: chip
    property string text: ""
    property bool selected: false
    signal picked()
    implicitWidth: chipText.implicitWidth + 20
    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: chip.selected ? Theme.accent
             : chipHit.containsMouse ? Theme.tileActive : Theme.tile
        border.width: chip.selected ? 0 : 1
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
        scale: chipHit.pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
    Text {
        id: chipText
        anchors.centerIn: parent
        text: chip.text
        color: chip.selected ? Theme.inverse : (chipHit.containsMouse ? Theme.ink : Theme.textDim)
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: chip.selected ? Font.DemiBold : Font.Medium
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    MouseArea {
        id: chipHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: chip.text
        onClicked: chip.picked()
    }
}
