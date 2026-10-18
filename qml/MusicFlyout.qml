import QtQuick

Flyout {
    id: musicFlyout

    property var root: null
    property var musicDialog: null

    objectName: "musicFlyout"
    open: root.musicOpen
    radius: Theme.radiusXl + 4
    baseY: root.shownY + root.pillH + 12

    width: musicPanel.implicitWidth
    height: musicPanel.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (root.width - width) / 2))

    readonly property real targetHeight: musicPanel.targetHeight

    MusicPanel {
        id: musicPanel
        anchors.fill: parent
        maxPanelWidth: root.width
        onCloseRequested: root.musicOpen = false
        onAddRequested: musicDialog.open()
        onToastRequested: (text) => root.showToast(text)
        onDropForwarded: (drop) => root.handleDrop(drop)
    }
}