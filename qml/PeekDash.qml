import QtQuick

// The small dash shown at the top edge while the notch is tucked away.
// Hovering/clicking it (or dragging over it) pulls the notch back out.
MouseArea {
    id: self

    property var root: null

    Theme { id: t }

    width: root.dashW + Math.round(6 * root.s)
    height: root.revealed ? (root.shownY + 2) : Math.max(7, root.dashH + 4)
    x: Math.round((root.width - width) / 2)
    y: 0
    hoverEnabled: true
    onClicked: if (!root.fullscreenGuard) root.revealed = true

    Rectangle {
        id: peekBar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 2
        width: root.dashW
        height: root.dashH
        radius: height / 2
        color: self.containsMouse ? t.ink : t.textDim
        opacity: root.clamp01(1 - root.progress * 3.5)
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    DropArea {
        id: peekDrop
        anchors.fill: parent
        onDropped: (drop) => root.handleDrop(drop)
    }

    readonly property bool containsDrag: peekDrop.containsDrag
    readonly property bool containsMouse: self.containsMouse
}