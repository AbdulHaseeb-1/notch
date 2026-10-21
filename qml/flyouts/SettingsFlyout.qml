import QtQuick

Flyout {
    id: panelItem

    property var root: null
    property var cfg: null

    open: panelItem.root.panelOpen
    radius: Theme.radiusXl
    baseY: panelItem.root.shownY + panelItem.root.pillH + 12

    width: settings.implicitWidth
    height: settings.implicitHeight
    x: Math.round(Math.max(Theme.screenMargin, (panelItem.root.width - width) / 2))

    readonly property real targetHeight: settings.targetHeight
    // Lets Main open Settings on a given tab (0 General, 1 Buddy).
    property alias tab: settings.tab

    SettingsPanel {
        id: settings
        anchors.fill: parent
        prefs: panelItem.root.settingsStore
        version: Dock.version
        startupSupported: Dock.startupSupported
        launchAtStartup: Dock.launchAtStartup
        maxPanelWidth: panelItem.root.width
        maxBodyHeight: panelItem.root.height - (panelItem.root.shownY + panelItem.root.pillH + 12) - settings.headerH - settings.footerH - 24

        onCloseRequested: panelItem.root.panelOpen = false
        onResetRequested: Dock.resetToDefaults()
        onQuitRequested: Dock.quit()
        onStartupToggled: (on) => Dock.launchAtStartup = on
        onRingPreviewRequested: panelItem.root.previewBuddyRing()
        onFxPreviewRequested: (kind) => panelItem.root.previewBuddyFx(kind)
        onResetBuddyPosRequested: {
            panelItem.root.settingsStore.buddyX = -1
            panelItem.root.settingsStore.buddyY = -1
            panelItem.root.showToast(qsTr("Buddy position reset"))
        }
    }
}