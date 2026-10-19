import QtQuick

Flyout {
    id: hubFlyout

    property var root: null
    property var sessionTimer: null

    objectName: "hubFlyout"
    open: hubFlyout.root.hubOpen
    radius: Theme.radiusXl
    baseY: hubFlyout.root.shownY + hubFlyout.root.pillH + 12

    width: hubPanel.implicitWidth
    height: hubPanel.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (hubFlyout.root.width - width) / 2))

    readonly property real targetHeight: hubPanel.targetHeight

    HubPanel {
        id: hubPanel
        anchors.fill: parent
        tab: hubFlyout.root.hubTab
        focusTimer: hubFlyout.root.hubTimer
        maxPanelWidth: hubFlyout.root.width
        maxBodyHeight: hubFlyout.root.height - (hubFlyout.root.shownY + hubFlyout.root.pillH + 12) - hubPanel.headerH - 24
        onCloseRequested: hubFlyout.root.hubOpen = false
        onTabPicked: (i) => hubFlyout.root.hubTab = i
        onToastRequested: (text) => hubFlyout.root.showToast(text)
    }
}