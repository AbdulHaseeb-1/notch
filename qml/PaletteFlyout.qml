import QtQuick

Flyout {
    id: paletteFlyout

    property var root: null

    objectName: "paletteFlyout"
    open: root.paletteOpen
    radius: Theme.radiusXl
    baseY: root.shownY + root.pillH + 12

    width: paletteCard.implicitWidth
    height: paletteCard.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (root.width - width) / 2))

    readonly property real targetHeight: paletteCard.targetHeight

    // Main.summonPalette() focuses the query field through this.
    function takeFocus() { paletteCard.takeFocus() }

    PaletteCard {
        id: paletteCard
        anchors.fill: parent
        onDismissed: root.closePalette()
    }
}