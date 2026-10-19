pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: ib
    property string icon
    property real glyphSize: 14
    property bool danger: false
    signal clicked()
    width: 26
    height: 26

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: ibHit.containsMouse ? (ib.danger ? Theme.dangerSoft : Theme.tile) : "transparent"
    }
    Glyph {
        anchors.centerIn: parent
        name: ib.icon
        size: ib.glyphSize
        color: ib.danger && ibHit.containsMouse ? Theme.danger
             : ibHit.containsMouse ? Theme.ink : Theme.textDim
        scale: ibHit.pressed ? 0.86 : 1
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
    MouseArea {
        id: ibHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: ib.clicked()
    }
}
