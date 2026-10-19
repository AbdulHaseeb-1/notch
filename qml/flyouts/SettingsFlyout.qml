import QtQuick

Flyout {
    id: panelItem

    property var root: null
    property var cfg: null
    property var fileDialog: null

    open: panelItem.root.panelOpen
    radius: Theme.radiusXl
    baseY: panelItem.root.shownY + panelItem.root.pillH + 12

    width: settings.implicitWidth
    height: settings.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (panelItem.root.width - width) / 2))

    readonly property real targetHeight: settings.targetHeight
    // Lets Main open Settings on a given tab (0 General, 1 Apps, 2 Buddy).
    property alias tab: settings.tab

    SettingsPanel {
        id: settings
        anchors.fill: parent
        prefs: panelItem.root.settingsStore
        dockModel: panelItem.root.apps
        runningList: panelItem.root.running
        version: Dock.version
        startupSupported: Dock.startupSupported
        launchAtStartup: Dock.launchAtStartup
        maxPanelWidth: panelItem.root.width
        maxBodyHeight: panelItem.root.height - (panelItem.root.shownY + panelItem.root.pillH + 12) - settings.headerH - settings.footerH - 24

        onCloseRequested: panelItem.root.panelOpen = false
        onAddRequested: panelItem.root.addAppDialog.open()
        onPinAppRequested: (name, path) => {
            if (Dock.pinApp(name, path))
                panelItem.root.showToast(qsTr("Pinned %1").arg(name))
        }
        onResetRequested: Dock.resetToDefaults()
        onQuitRequested: Dock.quit()
        onUnpinRequested: (i) => Dock.unpin(i)
        onMoveRequested: (from, to) => Dock.move(from, to)
        onStartupToggled: (on) => Dock.launchAtStartup = on
        onRingPreviewRequested: panelItem.root.previewBuddyRing()
        onResetBuddyPosRequested: {
            panelItem.root.settingsStore.buddyX = -1
            panelItem.root.settingsStore.buddyY = -1
            panelItem.root.showToast(qsTr("Buddy position reset"))
        }
    }
}