import QtQuick

Flyout {
    id: musicFlyout

    property var root: null
    property var musicDialog: null

    objectName: "musicFlyout"
    open: musicFlyout.root.musicOpen
    radius: Theme.radiusXl + 4
    baseY: musicFlyout.root.shownY + musicFlyout.root.pillH + 12

    width: musicPanel.implicitWidth
    height: musicPanel.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (musicFlyout.root.width - width) / 2))

    readonly property real targetHeight: musicPanel.targetHeight

    MusicPanel {
        id: musicPanel
        anchors.fill: parent
        maxPanelWidth: musicFlyout.root.width
        onCloseRequested: musicFlyout.root.musicOpen = false
        onAddRequested: musicFlyout.root.addMusicDialog.open()
        onToastRequested: (text) => musicFlyout.root.showToast(text)
        onDropForwarded: (drop) => musicFlyout.root.handleDrop(drop)
    }
}