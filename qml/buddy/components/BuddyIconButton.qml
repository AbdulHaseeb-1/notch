pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: ib
    property string icon: "close"
    property color tint: Theme.textDim
    property bool active: false
    property bool filledIcon: false
    property string tip: ""
    signal clicked()
    width: 28
    height: 28
    Rectangle {
        anchors.centerIn: parent
        width: 26
        height: 26
        radius: 13
        color: ibHit.pressed ? Theme.press : (ibHit.containsMouse ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Glyph {
        anchors.centerIn: parent
        name: ib.icon
        size: 14
        stroke: 1.7
        filled: ib.filledIcon
        color: ib.active ? ib.tint : (ibHit.containsMouse ? Theme.ink : ib.tint)
        scale: ibHit.pressed ? 0.86 : 1
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutBack } }
    }
    MouseArea {
        id: ibHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: ib.tip
        onClicked: ib.clicked()
    }
}
