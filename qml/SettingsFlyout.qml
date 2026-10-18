import QtQuick

Flyout {
    id: panelItem

    property var root: null
    property var cfg: null
    property var fileDialog: null

    open: root.panelOpen
    radius: Theme.radiusXl
    baseY: root.shownY + root.pillH + 12

    width: settings.implicitWidth
    height: settings.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (root.width - width) / 2))

    readonly property real targetHeight: settings.targetHeight
    // Lets Main open Settings on a given tab (0 General, 1 Apps, 2 Buddy).
    property alias tab: settings.tab

    SettingsPanel {
        id: settings
        anchors.fill: parent
        prefs: cfg
        dockModel: root.apps
        runningList: root.running
        version: dock.version
        startupSupported: dock.startupSupported
        launchAtStartup: dock.launchAtStartup
        maxPanelWidth: root.width
        maxBodyHeight: root.height - (root.shownY + root.pillH + 12) - settings.headerH - settings.footerH - 24

        onCloseRequested: root.panelOpen = false
        onAddRequested: fileDialog.open()
        onPinAppRequested: (name, path) => {
            if (dock.pinApp(name, path))
                root.showToast(qsTr("Pinned %1").arg(name))
        }
        onResetRequested: dock.resetToDefaults()
        onQuitRequested: dock.quit()
        onUnpinRequested: (i) => dock.unpin(i)
        onMoveRequested: (from, to) => dock.move(from, to)
        onStartupToggled: (on) => dock.launchAtStartup = on
        onResetBuddyPosRequested: {
            cfg.buddyX = -1
            cfg.buddyY = -1
            root.showToast(qsTr("Buddy position reset"))
        }
    }
}