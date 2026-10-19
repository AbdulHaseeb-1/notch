pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: spb
    property string label
    property string icon: ""
    property bool primary: false
    property bool danger: false
    signal clicked()
    implicitWidth: spbRow.implicitWidth + 22
    implicitHeight: 28
    opacity: spb.enabled ? 1 : 0.5

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: spb.primary ? (spbHit.containsMouse ? Theme.accentHover : Theme.accent)
             : spb.danger ? (spbHit.containsMouse ? Theme.dangerSoft : "transparent")
             : (spbHit.containsMouse ? Theme.tileActive : Theme.tile)
        border.width: spb.primary ? 0 : 1
        border.color: spb.danger && spbHit.containsMouse ? Theme.danger : Theme.border
        scale: spbHit.pressed ? 0.95 : 1
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }
    Row {
        id: spbRow
        anchors.centerIn: parent
        spacing: 5
        Glyph {
            visible: spb.icon.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: spb.icon
            size: 12
            stroke: 2.1
            color: spb.primary ? Theme.inverse : (spb.danger ? Theme.danger : Theme.ink)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: spb.label
            color: spb.primary ? Theme.inverse : (spb.danger ? Theme.danger : Theme.ink)
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
            font.weight: Font.Bold
        }
    }
    MouseArea {
        id: spbHit
        anchors.fill: parent
        hoverEnabled: true
        enabled: spb.enabled
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: spb.label
        onClicked: spb.clicked()
    }
}
