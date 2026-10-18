import QtQuick

Flyout {
    id: paletteFlyout

    property var root: null

    objectName: "paletteFlyout"
    open: root.paletteOpen
    radius: 22
    baseY: root.shownY + root.pillH + 12

    width: paletteCard.implicitWidth
    height: paletteCard.implicitHeight
    x: Math.round((root.width - width) / 2)

    readonly property real targetHeight: paletteCard.targetHeight

    PaletteCard {
        id: paletteCard
        anchors.fill: parent
        onDismissed: root.closePalette()
    }
}