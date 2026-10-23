pragma ComponentBehavior: Bound
import QtQuick

// A pickable tile with a preview on top and a name below (ring styles, glow modes).
// Put the preview inside it. `lit` is true only while the tile is hovered, so a live
// preview moves only when it is being looked at. (It used to move for the selected
// tile too, which kept the page redrawing every frame for as long as it was open.)
Item {
    id: tile
    property string label: ""
    property bool selected: false
    readonly property bool lit: hit.containsMouse
    default property alias preview: stage.data
    property real stageHeight: 76
    signal picked()

    implicitWidth: 120
    implicitHeight: stageHeight + 30
    Accessible.role: Accessible.RadioButton
    Accessible.name: label
    Accessible.checked: selected

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: tile.selected ? Theme.tileActive : (hit.containsMouse ? Theme.glassFillHover : Theme.glassFill)
        border.width: 1
        border.color: tile.selected ? Theme.accent : Theme.glassBorder
        Behavior on color { ColorAnimation { duration: 130 } }
        Behavior on border.color { ColorAnimation { duration: 130 } }
        scale: hit.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
    Item {
        id: stage
        x: 0
        y: 4
        width: parent.width
        height: tile.stageHeight
        clip: true
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: tile.stageHeight + 8
        text: tile.label
        color: tile.selected ? Theme.ink : Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: tile.selected ? Font.Bold : Font.DemiBold
    }
    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.picked()
    }
}
