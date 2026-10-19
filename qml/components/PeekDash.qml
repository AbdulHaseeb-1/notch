import QtQuick

// The small dash shown at the top edge while the notch is tucked away.
// Hovering/clicking it (or dragging over it) pulls the notch back out.
MouseArea {
    id: self

    property var root: null


    width: self.root.dashW + Math.round(6 * self.root.s)
    height: self.root.revealed ? (self.root.shownY + 2) : Math.max(7, self.root.dashH + 4)
    x: Math.round((self.root.width - width) / 2)
    y: 0
    hoverEnabled: true
    onClicked: if (!self.root.fullscreenGuard) self.root.revealed = true

    Rectangle {
        id: peekBar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 2
        width: self.root.dashW
        height: self.root.dashH
        radius: height / 2
        color: self.containsMouse ? Theme.ink : Theme.textDim
        opacity: self.root.clamp01(1 - self.root.progress * 3.5)
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    DropArea {
        id: peekDrop
        anchors.fill: parent
        onDropped: (drop) => self.root.handleDrop(drop)
    }

    readonly property bool containsDrag: peekDrop.containsDrag
    readonly property bool containsMouse: self.containsMouse
}