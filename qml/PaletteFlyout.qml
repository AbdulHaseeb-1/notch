import QtQuick

Flyout {
    id: paletteFlyout

    property var root: null

    objectName: "paletteFlyout"
    open: paletteFlyout.root.paletteOpen
    radius: Theme.radiusXl
    baseY: paletteFlyout.root.shownY + paletteFlyout.root.pillH + 12

    width: paletteCard.implicitWidth
    height: paletteCard.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (paletteFlyout.root.width - width) / 2))

    readonly property real targetHeight: paletteCard.targetHeight

    // Main.summonPalette() focuses the query field through this.
    function takeFocus() { paletteCard.takeFocus() }

    PaletteCard {
        id: paletteCard
        anchors.fill: parent
        maxPanelWidth: paletteFlyout.root.width
        onDismissed: paletteFlyout.root.closePalette()
    }
}