import QtQuick

// The small handle under the notch used to drag-resize it.
MouseArea {
    id: self

    property var root: null
    property var cfg: null
    property var pillItem: null
    property bool pillHovered: false


    width: Math.round(64 * root.s)
    height: 16
    x: (root.width - width) / 2
    y: (pillItem ? pillItem.y + pillItem.height : 0) - 6
    hoverEnabled: true
    enabled: root.revealed
    cursorShape: Qt.SizeVerCursor
    preventStealing: true

    property real startY: 0
    property real startScale: 1

    onPressed: (mouse) => {
        startY = mapToItem(null, mouse.x, mouse.y).y
        startScale = cfg.sizeScale
        root.resizing = true
    }
    onPositionChanged: (mouse) => {
        if (!pressed)
            return
        const dy = mapToItem(null, mouse.x, mouse.y).y - startY
        root.setScale(startScale + dy / 58)
    }
    onReleased: { root.resizing = false; root.hudTimer.restart() }
    onCanceled: root.resizing = false

    readonly property bool showBar: containsMouse || pressed
        || (pillHovered && !root.tipVisible && root.menuIndex < 0)

    // Expose hovered state name used by root? root references grip.containsMouse already via id.
    Rectangle {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 0
        width: self.containsMouse || self.pressed ? 46 : 30
        height: self.pressed ? 5 : 4
        radius: height / 2
        color: self.containsMouse || self.pressed ? Theme.ink : Theme.textDim
        border.width: 1
        border.color: Theme.surface
        opacity: self.showBar && root.progress > 0.9 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }
}