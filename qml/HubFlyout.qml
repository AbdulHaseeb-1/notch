import QtQuick

Flyout {
    id: hubFlyout

    property var root: null
    property var sessionTimer: null

    objectName: "hubFlyout"
    open: root.hubOpen
    radius: 22
    baseY: root.shownY + root.pillH + 12

    width: hubPanel.implicitWidth
    height: hubPanel.implicitHeight
    x: Math.round(Math.max(12, (root.width - width) / 2))

    readonly property real targetHeight: hubPanel.targetHeight

    HubPanel {
        id: hubPanel
        anchors.fill: parent
        tab: root.hubTab
        focusTimer: sessionTimer
        maxBodyHeight: root.height - (root.shownY + root.pillH + 12) - hubPanel.headerH - 24
        onCloseRequested: root.hubOpen = false
        onTabPicked: (i) => root.hubTab = i
        onToastRequested: (text) => root.showToast(text)
    }
}