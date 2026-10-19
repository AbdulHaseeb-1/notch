pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: ih
    property string icon
    property real glyphSize: 16
    property color color: Theme.textDim
    property color hoverColor: Theme.ink
    property bool filled: false
    property real stroke: 1.8
    readonly property bool hovered: ihHit.containsMouse
    signal clicked()
    width: 32
    height: 32

    Glyph {
        anchors.centerIn: parent
        name: ih.icon
        size: ih.glyphSize
        stroke: ih.stroke
        filled: ih.filled
        color: ihHit.containsMouse ? ih.hoverColor : ih.color
        scale: ihHit.pressed ? 0.86 : 1
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on scale { NumberAnimation { duration: 110 } }
    }
    MouseArea {
        id: ihHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: ih.clicked()
    }
}
