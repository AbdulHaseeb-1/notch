pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Wide, solid-surface Settings panel under the notch.
// Organized into three spacious pages:
//   0 — General (Scale bar, 2x2 Appearance grid, Behavior & System)
//   1 — Apps    (Side-by-side Pinned in Notch + Searchable Installed Apps)
//   2 — Buddy   (Desktop Buddy controls + AI Provider configuration)
Item {
    id: panel

    property var prefs
    property var dockModel: []
    property var runningList: []
    property bool startupSupported: false
    property bool launchAtStartup: false
    property bool blurSupported: false
    property bool blurBehind: false
    property real maxPanelWidth: 620
    property real maxBodyHeight: 100000
    property string version: ""

    signal closeRequested()
    signal addRequested()
    signal pinAppRequested(string name, string path)
    signal resetRequested()
    signal quitRequested()
    signal unpinRequested(int index)
    signal moveRequested(int from, int to)
    signal startupToggled(bool enabled)
    signal blurToggled(bool on)
    signal resetBuddyPosRequested()
    signal ringPreviewRequested()


    property int tab: 0
    focus: true
    Keys.onPressed: (event) => {
        if (event.modifiers & Qt.ControlModifier) {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                tab = (tab + (event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? 2 : 1)) % 3
                event.accepted = true
            } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_3) {
                tab = event.key - Qt.Key_1
                event.accepted = true
            }
        }
    }
    property string appFilter: ""
    property var availableList: []

    // ---- AI provider (Buddy tab) -----------------------------------------
    readonly property var providerInfo: ({
        openai: {
            label: "OpenAI", base: "https://api.openai.com", needsKey: true,
            models: ["gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1", "o4-mini"]
        },
        ollama: {
            label: "Ollama", base: "http://localhost:11434", needsKey: false,
            models: ["llama3.2", "llama3.1", "qwen2.5", "gemma3"]
        },
        anthropic: {
            label: "Anthropic", base: "https://api.anthropic.com", needsKey: true,
            models: ["claude-sonnet-4-6", "claude-opus-4-6", "claude-sonnet-4-5",
                     "claude-opus-4-1", "claude-3-7-sonnet-latest", "claude-3-5-haiku-latest"]
        },
        custom: { label: "Custom", base: "", needsKey: false, models: [] }
    })
    property string lastProvider: ""
    property var providerMemory: ({})
    property var remoteModels: []
    property string testState: "idle"     // idle | testing | ok | failed
    property string testMessage: ""

    readonly property bool realChat: prefs && prefs.buddyProvider !== "none"
    readonly property bool buddyMovedCustom: prefs && (prefs.buddyX >= 0 || prefs.buddyY >= 0)

    function infoFor(p) { return panel.providerInfo[p] || panel.providerInfo.custom }
    function presetsFor(p) { return panel.infoFor(p).models }
    function labelFor(p) { return panel.infoFor(p).label }
    function showsKeyFor(p) { return p !== "ollama" }

    function chatEndpoint() {
        const p = panel.prefs ? panel.prefs.buddyProvider : "none"
        const base = panel.prefs ? panel.prefs.buddyApiBase : ""
        if (BuddyAgent)
            return BuddyAgent.chatUrl()
        const b = (String(base).trim() || panel.infoFor(p).base).replace(/\/+$/, "")
        if (p === "ollama")
            return b + "/api/chat"
        if (p === "anthropic")
            return b + "/v1/messages"
        return b + "/v1/chat/completions"
    }

    readonly property var modelChips: {
        const out = []
        const add = (m) => {
            if (m && out.indexOf(m) < 0 && out.length < 10)
                out.push(m)
        }
        const presets = panel.presetsFor(panel.prefs ? panel.prefs.buddyProvider : "none")
        for (let i = 0; i < presets.length; ++i)
            add(presets[i])
        for (let i = 0; i < panel.remoteModels.length; ++i)
            add(panel.remoteModels[i])
        return out
    }

    function markDirty() {
        panel.testState = "idle"
        panel.testMessage = ""
        panel.remoteModels = []
    }

    function setModel(m) {
        panel.prefs.buddyModel = m
        panel.markDirty()
    }

    function pickProvider(v) {
        const from = panel.prefs.buddyProvider
        if (v === from || !v || v === "none")
            return
        panel.providerMemory[from] = {
            base: panel.prefs.buddyApiBase,
            model: panel.prefs.buddyModel
        }
        panel.prefs.buddyProvider = v
        panel.lastProvider = v

        const was = panel.providerMemory[v]
        const presets = panel.presetsFor(v)
        panel.prefs.buddyApiBase = (was && was.base !== undefined)
            ? was.base : panel.infoFor(v).base
        panel.prefs.buddyModel = (was && was.model && was.model.length > 0)
            ? was.model : (presets.length > 0 ? presets[0] : "")
        panel.markDirty()
    }

    function setRealChat(on) {
        if (!on) {
            if (panel.prefs.buddyProvider !== "none")
                panel.lastProvider = panel.prefs.buddyProvider
            panel.prefs.buddyProvider = "none"
            return
        }
        panel.pickProvider(panel.lastProvider !== "" ? panel.lastProvider : "openai")
    }

    function runTest() {
        panel.testState = "testing"
        panel.testMessage = qsTr("Checking %1…").arg(BuddyAgent.modelsUrl())
        BuddyAgent.probe()
    }

    Connections {
        target: BuddyAgent
        function onTestFinished(ok, message) {
            panel.testState = ok ? "ok" : "failed"
            panel.testMessage = message
        }
        function onModelsFetched(models) {
            panel.remoteModels = models || []
        }
    }

    readonly property real headerH: 64
    readonly property real footerH: 50

    // Wide by default, always clamped to the screen. Below ~760 px the
    // two-column pages fall back to a single column.
    implicitWidth: Theme.fit(Theme.settingsWidth, maxPanelWidth)
    readonly property real bodyW: width - 40
    readonly property bool wide: bodyW >= 720
    readonly property real colGap: 16
    readonly property real colW: wide ? Math.floor((bodyW - colGap) / 2) : bodyW
    // The Apps lists grow with the screen instead of a fixed 322 px.
    readonly property real appsListH: Math.max(240, Math.min(460, maxBodyHeight - 80))
    implicitHeight: headerH + 1 + content.height + 1 + footerH

    readonly property real naturalBodyHeight: tab === 0
        ? general.implicitHeight
        : (tab === 1 ? appsView.implicitHeight : buddyCol.implicitHeight)
    readonly property real targetBodyHeight: Math.min(naturalBodyHeight, maxBodyHeight)
    readonly property real targetHeight: headerH + 1 + targetBodyHeight + 1 + footerH

    function refreshAvailableApps() {
        const raw = AppIndex.availableApps(panel.appFilter)
        const filtered = []
        for (let i = 0; i < raw.length; ++i) {
            const item = raw[i]
            if (!Dock.isPinned(item.path || "", item.exe || ""))
                filtered.push(item)
        }
        panel.availableList = filtered
    }

    onDockModelChanged: refreshAvailableApps()
    onRunningListChanged: if (panel.visible) refreshAvailableApps()
    onAppFilterChanged: refreshAvailableApps()
    onTabChanged: if (tab === 1) refreshAvailableApps()
    Component.onCompleted: refreshAvailableApps()

    Connections {
        target: AppIndex
        function onIndexReady() {
            if (panel.visible)
                panel.refreshAvailableApps()
        }
    }

    // ---------- reusable components -------------------------------------------
    // ---- Buddy ring colour ---------------------------------------------------
    // The neon halo presets come from C++ (SystemAppearance), with their
    // gradient stops already resolved.
    readonly property var ringOpts: SystemAppearance.ringOptions()
    function optionLabel(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i].label
        return ""
    }


    // ---------- header --------------------------------------------------------
    Item {
        id: header
        width: parent.width
        height: panel.headerH

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                text: qsTr("Settings")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontLg
                font.weight: Font.Bold
            }
            Text {
                text: panel.tab === 0 ? qsTr("Size, notch & system behavior")
                    : (panel.tab === 1 ? qsTr("Manage pinned dock & installed apps")
                                       : qsTr("Buddy, AI provider & agent tools"))
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
            }
        }

        Segmented {
            anchors.right: closeBtn.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(340, panel.width - 260)
            height: 34
            options: [
                { label: qsTr("General"), value: 0 },
                { label: qsTr("Apps (%1)").arg(panel.dockModel.length), value: 1 },
                { label: qsTr("Buddy"), value: 2 }
            ]
            value: panel.tab
            onPicked: (v) => panel.tab = v
        }

        SettingsIconButton {
            id: closeBtn
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            icon: "close"
            glyphSize: 15
            onClicked: panel.closeRequested()
        }
    }

    Rectangle {
        id: topRule
        anchors.top: header.bottom
        width: parent.width
        height: 1
        color: Theme.borderSoft
    }

    // ---------- body ----------------------------------------------------------
    Item {
        id: content
        anchors.top: topRule.bottom
        width: parent.width
        height: panel.targetBodyHeight
        clip: true
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // ==== Tab 0: General ==================================================
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: general.implicitHeight
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 3500
            clip: true
            visible: panel.tab === 0 || general.opacity > 0.01
            z: panel.tab === 0 ? 1 : 0

            Flow {
                id: general
                x: 20 - (panel.tab === 0 ? 0 : 16)
                width: content.width - 40
                topPadding: 16
                bottomPadding: 18
                spacing: panel.colGap
                opacity: panel.tab === 0 ? 1 : 0
                enabled: panel.tab === 0
                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                Column {
                    width: panel.colW
                    spacing: 12

                    SettingsSectionHeader {
                        title: qsTr("Size & scale")
                        rightText: Math.round(panel.prefs.sizeScale * 100) + "%"
                    }

                    SettingsGroupCard {
                        Item {
                            width: parent.width
                            height: 32

                            Segmented {
                                id: sizePresets
                                width: Math.min(200, parent.width * 0.5)
                                height: 32
                                anchors.verticalCenter: parent.verticalCenter
                                options: [
                                    { label: "S", value: 0.8 },
                                    { label: "M", value: 1.0 },
                                    { label: "L", value: 1.2 },
                                    { label: "XL", value: 1.4 }
                                ]
                                value: panel.prefs.sizeScale
                                onPicked: (v) => panel.prefs.sizeScale = v
                            }

                            SizeSlider {
                                anchors.left: sizePresets.right
                                anchors.leftMargin: 14
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                from: 0.7
                                to: 1.6
                                value: panel.prefs.sizeScale
                                onMoved: (v) => panel.prefs.sizeScale = Math.round(v * 100) / 100
                            }
                        }

                        Text {
                            width: parent.width
                            text: qsTr("Drag the handle below the notch, or Ctrl + scroll on it, to resize live.")
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }
                    }

                    SettingsSectionHeader { title: qsTr("Notch") }

                    SettingsGroupCard {
                        SettingsToggleRow {
                            label: qsTr("Monochrome app icons")
                            sub: qsTr("Render dock icons in quiet monochrome that sits well on dark stone.")
                            checked: panel.prefs.monoIcons
                            onToggled: (c) => panel.prefs.monoIcons = c
                        }
                        SettingsCardDivider {}
                        SettingsToggleRow {
                            label: qsTr("Show clock")
                            sub: qsTr("Time and date on the right of the notch. Click it for the timer and clipboard.")
                            checked: panel.prefs.showClock
                            onToggled: (c) => panel.prefs.showClock = c
                        }
                    }
                }

                Column {
                    width: panel.colW
                    spacing: 12

                    SettingsSectionHeader { title: qsTr("Behavior & system") }

                    SettingsGroupCard {
                        SettingsToggleRow {
                            label: qsTr("Auto-hide notch")
                            sub: qsTr("Tucks into the top edge; hover the top dash to reveal.")
                            checked: panel.prefs.autoHide
                            onToggled: (c) => panel.prefs.autoHide = c
                        }

                        Item {
                            visible: panel.prefs.autoHide
                            width: parent.width
                            height: 32
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Hide delay")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontBase
                                font.weight: Font.Medium
                            }
                            Segmented {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 268
                                height: 28
                                options: [
                                    { label: "0.3s", value: 300 },
                                    { label: "0.8s", value: 800 },
                                    { label: "1.2s", value: 1200 },
                                    { label: "1.5s", value: 1500 },
                                    { label: "3s", value: 3000 }
                                ]
                                value: panel.prefs.hideDelay
                                onPicked: (v) => panel.prefs.hideDelay = v
                            }
                        }

                        SettingsCardDivider {}
                        SettingsToggleRow {
                            label: qsTr("Quiet in fullscreen")
                            sub: qsTr("Ignore top-edge hover while a game or fullscreen video is active.")
                            checked: panel.prefs.suppressInFullscreen
                            onToggled: (c) => panel.prefs.suppressInFullscreen = c
                        }
                        SettingsCardDivider {}
                        Item {
                            width: parent.width
                            implicitHeight: buddyToggle.implicitHeight

                            SettingsToggleRow {
                                id: buddyToggle
                                width: parent.width - (resetPosBtn.visible ? resetPosBtn.width + 12 : 0)
                                label: qsTr("Desktop buddy")
                                sub: qsTr("Interactive orb companion. Drag it anywhere or click it for tasks, timers and AI chat.")
                                checked: panel.prefs.buddyEnabled
                                onToggled: (c) => panel.prefs.buddyEnabled = c
                            }
                            SettingsPillButton {
                                id: resetPosBtn
                                visible: panel.buddyMovedCustom
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "reset"
                                label: qsTr("Reset position")
                                onClicked: panel.resetBuddyPosRequested()
                            }
                        }
                        SettingsCardDivider {}
                        SettingsToggleRow {
                            label: qsTr("Command palette hotkey")
                            sub: !panel.prefs.hotkeyPalette
                                 ? qsTr("Off. Ctrl + Alt + Space summons search from anywhere.")
                                 : (!Hotkey.registered
                                    ? qsTr("Shortcut unavailable on this PC.")
                                    : (Hotkey.preferred
                                       ? qsTr("Press Ctrl + Alt + Space anywhere to search.")
                                       : qsTr("Active on %1.").arg(Hotkey.shortcut)))
                            checked: panel.prefs.hotkeyPalette
                            onToggled: (c) => panel.prefs.hotkeyPalette = c
                        }
                        SettingsCardDivider { visible: panel.startupSupported }
                        SettingsToggleRow {
                            visible: panel.startupSupported
                            label: qsTr("Launch at startup")
                            sub: qsTr("Start Noch automatically when you sign in to Windows.")
                            checked: panel.launchAtStartup
                            onToggled: (c) => panel.startupToggled(c)
                        }
                    }
                }
            }
        }

        // ==== Tab 1: Apps (Side-by-Side 2-Column Split View) ==================
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: appsView.implicitHeight
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            visible: panel.tab === 1 || appsView.opacity > 0.01
            z: panel.tab === 1 ? 1 : 0

            Item {
                id: appsView
                x: 20 + (panel.tab === 1 ? 0 : 16)
                width: content.width - 40
                implicitHeight: panel.appsListH + 72
                height: implicitHeight
                opacity: panel.tab === 1 ? 1 : 0
                enabled: panel.tab === 1
                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                readonly property real colGap: 14
                readonly property real leftW: Math.floor((width - colGap) * 0.47)
                readonly property real rightW: width - leftW - colGap

                // ── Left Column: Pinned in Notch ──────────────────────────────
                Column {
                    x: 0
                    y: 16
                    width: appsView.leftW
                    spacing: 10

                    Item {
                        width: parent.width
                        height: 28

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("PINNED (%1)").arg(panel.dockModel.length)
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            SettingsPillButton {
                                icon: "reset"
                                label: qsTr("Defaults")
                                onClicked: panel.resetRequested()
                            }
                            SettingsPillButton {
                                icon: "folder"
                                label: qsTr("Browse…")
                                onClicked: panel.addRequested()
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: panel.appsListH
                        radius: 14
                        color: Theme.raised
                        border.width: 0
                        border.color: Theme.borderSoft
                        clip: true

                        Text {
                            visible: panel.dockModel.length === 0
                            anchors.centerIn: parent
                            width: parent.width - 32
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("No apps pinned yet.\nClick + Add on the right to pin apps.")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                        }

                        ListView {
                            id: pinnedList
                            visible: panel.dockModel.length > 0
                            x: 6
                            y: 6
                            width: parent.width - 12
                            height: parent.height - 12
                            clip: true
                            interactive: contentHeight > height
                            boundsBehavior: Flickable.StopAtBounds
                            cacheBuffer: 320
                            model: panel.dockModel
                            spacing: 2

                            displaced: Transition {
                                NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
                            }

                            delegate: Item {
                                id: appRow
                                required property int index
                                required property var modelData
                                width: pinnedList.width
                                height: 40

                                readonly property bool isRunning: panel.runningList.indexOf(modelData.exe) >= 0

                                HoverHandler { id: rowHover }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: rowHover.hovered ? Theme.hover : "transparent"
                                }

                                Item {
                                    id: rowIcon
                                    x: 10
                                    width: 22
                                    height: 22
                                    anchors.verticalCenter: parent.verticalCenter

                                    Image {
                                        id: rowImg
                                        anchors.fill: parent
                                        source: appRow.modelData.iconId
                                                ? "image://appicon/" + (panel.prefs.monoIcons ? Theme.iconMono : "color") + "/" + appRow.modelData.iconId
                                                : ""
                                        sourceSize: Qt.size(96, 96)
                                        mipmap: true
                                        smooth: true
                                        fillMode: Image.PreserveAspectFit
                                        visible: status === Image.Ready
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: rowImg.status !== Image.Ready
                                        radius: 6
                                        color: Theme.tile
                                        border.width: 0
                                        border.color: Theme.border
                                        Glyph {
                                            anchors.centerIn: parent
                                            name: Theme.appGlyph(appRow.modelData.name)
                                            size: Math.round(parent.height * 0.58)
                                            stroke: 1.6
                                            color: Theme.inkSoft
                                        }
                                    }
                                }

                                Row {
                                    anchors.left: rowIcon.right
                                    anchors.leftMargin: 10
                                    anchors.right: rowActions.left
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6

                                    Text {
                                        width: Math.min(implicitWidth, parent.width - (appRow.isRunning ? 14 : 0))
                                        text: appRow.modelData.name
                                        color: Theme.ink
                                        elide: Text.ElideRight
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontMd
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        visible: appRow.isRunning
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 6
                                        height: 6
                                        radius: 3
                                        color: Theme.success
                                    }
                                }

                                Row {
                                    id: rowActions
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    opacity: rowHover.hovered ? 1 : 0.65
                                    Behavior on opacity { NumberAnimation { duration: 110 } }

                                    SettingsIconButton {
                                        icon: "up"
                                        glyphSize: 13
                                        visible: appRow.index > 0
                                        onClicked: panel.moveRequested(appRow.index, appRow.index - 1)
                                    }
                                    SettingsIconButton {
                                        icon: "down"
                                        glyphSize: 13
                                        visible: appRow.index < panel.dockModel.length - 1
                                        onClicked: panel.moveRequested(appRow.index, appRow.index + 1)
                                    }
                                    SettingsIconButton {
                                        icon: "close"
                                        glyphSize: 13
                                        danger: true
                                        onClicked: panel.unpinRequested(appRow.index)
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Right Column: Installed Apps List to Add ──────────────────
                Column {
                    x: appsView.leftW + appsView.colGap
                    y: 16
                    width: appsView.rightW
                    spacing: 10

                    Item {
                        width: parent.width
                        height: 28

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("ADD APPS")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 available").arg(panel.availableList.length)
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: panel.appsListH
                        radius: 14
                        color: Theme.raised
                        border.width: 0
                        border.color: Theme.borderSoft
                        clip: true

                        // Search bar pinned at the top of the right card
                        Rectangle {
                            id: filterBar
                            x: 10
                            y: 10
                            width: parent.width - 20
                            height: 34
                            radius: 9
                            color: Theme.field
                            border.width: 1
                            border.color: filterInput.activeFocus ? Theme.accent : Theme.fieldBorder
                            Behavior on border.color { ColorAnimation { duration: 130 } }

                            Glyph {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                name: "search"
                                size: 13
                                stroke: 2
                                color: filterInput.activeFocus ? Theme.accent : Theme.textDim
                            }

                            TextInput {
                                id: filterInput
                                anchors.left: parent.left
                                anchors.leftMargin: 30
                                anchors.right: clearFilterBtn.visible ? clearFilterBtn.left : parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.ink
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.inverse
                                font.family: Theme.font
                                font.pixelSize: Theme.fontMd
                                font.weight: Font.Medium
                                clip: true
                                selectByMouse: true
                                onTextChanged: panel.appFilter = text

                                Text {
                                    anchors.fill: parent
                                    visible: filterInput.text.length === 0
                                    verticalAlignment: TextInput.AlignVCenter
                                    text: qsTr("Search installed apps…")
                                    color: Theme.textFaint
                                    font: filterInput.font
                                }
                            }

                            SettingsIconButton {
                                id: clearFilterBtn
                                visible: filterInput.text.length > 0
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                width: 24
                                height: 24
                                icon: "close"
                                glyphSize: 11
                                onClicked: {
                                    filterInput.text = ""
                                    filterInput.forceActiveFocus()
                                }
                            }
                        }

                        Text {
                            visible: panel.availableList.length === 0
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: 16
                            text: filterInput.text.length > 0
                                  ? qsTr("No matching unpinned apps found")
                                  : qsTr("All detected apps are already pinned")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                        }

                        ListView {
                            id: addList
                            visible: panel.availableList.length > 0
                            x: 6
                            y: 50
                            width: parent.width - 12
                            height: parent.height - 56
                            clip: true
                            interactive: contentHeight > height
                            boundsBehavior: Flickable.StopAtBounds
                            cacheBuffer: 320
                            model: panel.availableList
                            spacing: 2

                            displaced: Transition {
                                NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
                            }

                            delegate: Item {
                                id: candRow
                                required property int index
                                required property var modelData
                                width: addList.width
                                height: 40

                                HoverHandler { id: candHover }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: candHover.hovered ? Theme.hover : "transparent"
                                }

                                Item {
                                    id: candIcon
                                    x: 10
                                    width: 22
                                    height: 22
                                    anchors.verticalCenter: parent.verticalCenter

                                    Image {
                                        id: candImg
                                        anchors.fill: parent
                                        source: candRow.modelData.iconId
                                                ? "image://appicon/color/" + candRow.modelData.iconId
                                                : ""
                                        sourceSize: Qt.size(96, 96)
                                        mipmap: true
                                        smooth: true
                                        fillMode: Image.PreserveAspectFit
                                        visible: status === Image.Ready
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: candImg.status !== Image.Ready
                                        radius: 6
                                        color: Theme.tile
                                        border.width: 0
                                        border.color: Theme.border
                                        Glyph {
                                            anchors.centerIn: parent
                                            name: Theme.appGlyph(candRow.modelData.name)
                                            size: Math.round(parent.height * 0.58)
                                            stroke: 1.6
                                            color: Theme.inkSoft
                                        }
                                    }
                                }

                                Text {
                                    anchors.left: candIcon.right
                                    anchors.leftMargin: 10
                                    anchors.right: addPill.left
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: candRow.modelData.name
                                    color: Theme.ink
                                    elide: Text.ElideRight
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontMd
                                    font.weight: Font.DemiBold
                                }

                                Rectangle {
                                    id: addPill
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: addPillRow.implicitWidth + 16
                                    height: 25
                                    radius: 12.5
                                    color: candHover.hovered ? Theme.accent : Theme.tile
                                    border.width: candHover.hovered ? 0 : 1
                                    border.color: Theme.border
                                    scale: candTap.pressed ? 0.94 : 1
                                    Behavior on scale { NumberAnimation { duration: 100 } }
                                    Behavior on color { ColorAnimation { duration: 110 } }

                                    Row {
                                        id: addPillRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Glyph {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "plus"
                                            size: 11
                                            stroke: 2.2
                                            color: candHover.hovered ? Theme.inverse : Theme.ink
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("Add")
                                            color: candHover.hovered ? Theme.inverse : Theme.ink
                                            font.family: Theme.font
                                            font.pixelSize: Theme.fontBase
                                            font.weight: Font.Bold
                                        }
                                    }
                                }

                                TapHandler {
                                    id: candTap
                                    gesturePolicy: TapHandler.ReleaseWithinBounds
                                    onTapped: panel.pinAppRequested(candRow.modelData.name, candRow.modelData.path)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ==== Tab 2: Buddy AI Provider (Properly parented inside `content`) ===
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: buddyCol.implicitHeight
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            visible: panel.tab === 2 || buddyCol.opacity > 0.01
            z: panel.tab === 2 ? 1 : 0

            Flow {
                id: buddyCol
                x: 20 + (panel.tab === 2 ? 0 : 16)
                width: content.width - 40
                topPadding: 16
                bottomPadding: 18
                spacing: panel.colGap
                opacity: panel.tab === 2 ? 1 : 0
                enabled: panel.tab === 2
                Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                // Left: the buddy itself and the model it talks to.
                Column {
                    width: panel.colW
                    spacing: 12

                    SettingsSectionHeader {
                        title: qsTr("Desktop buddy")
                        rightText: panel.prefs.buddyEnabled
                                     ? (panel.realChat ? panel.labelFor(panel.prefs.buddyProvider) : qsTr("Local mode"))
                                     : qsTr("Disabled")
                    }

                    SettingsGroupCard {
                        Item {
                            width: parent.width
                            implicitHeight: enableBuddyRow.implicitHeight

                            SettingsToggleRow {
                                id: enableBuddyRow
                                width: parent.width - (resetBuddyBtn2.visible ? resetBuddyBtn2.width + 12 : 0)
                                label: qsTr("Enable desktop buddy")
                                sub: qsTr("Show the interactive orb on your desktop. Drag to move, click to ask.")
                                checked: panel.prefs.buddyEnabled
                                onToggled: (c) => panel.prefs.buddyEnabled = c
                            }

                            SettingsPillButton {
                                id: resetBuddyBtn2
                                visible: panel.buddyMovedCustom
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "reset"
                                label: qsTr("Reset position")
                                onClicked: panel.resetBuddyPosRequested()
                            }
                        }

                        SettingsCardDivider {}

                        Item {
                            width: parent.width
                            implicitHeight: sfxToggleRow.implicitHeight

                            SettingsToggleRow {
                                id: sfxToggleRow
                                width: parent.width - (previewSfxBtn.width + 12)
                                label: qsTr("Buddy sound effects")
                                sub: qsTr("Expressive companion chirps, purrs, and chimes when interacting.")
                                checked: panel.prefs.buddySfxEnabled
                                onToggled: (c) => {
                                    panel.prefs.buddySfxEnabled = c
                                    if (c && BuddySound)
                                        Qt.callLater(function() { BuddySound.play("wake") })
                                }
                            }

                            SettingsPillButton {
                                id: previewSfxBtn
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon: panel.prefs.buddySfxEnabled ? "volume" : "mute"
                                label: qsTr("Test sound")
                                enabled: panel.prefs.buddySfxEnabled
                                onClicked: {
                                    if (BuddySound)
                                        BuddySound.play("happy")
                                }
                            }
                        }

                        Row {
                            visible: panel.prefs.buddySfxEnabled
                            width: parent.width
                            spacing: 14

                            Column {
                                width: Math.floor((parent.width - 14) * 0.54)
                                spacing: 6

                                Text {
                                    width: parent.width
                                    text: qsTr("Voice style")
                                    color: Theme.textDim
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontSm
                                    font.weight: Font.DemiBold
                                }

                                Segmented {
                                    width: parent.width
                                    height: 30
                                    options: [
                                        { label: qsTr("Cute"), value: "cute" },
                                        { label: qsTr("Warm"), value: "warm" },
                                        { label: qsTr("Mellow"), value: "mellow" }
                                    ]
                                    value: panel.prefs.buddySfxStyle
                                    onPicked: (v) => {
                                        panel.prefs.buddySfxStyle = v
                                        if (BuddySound)
                                            Qt.callLater(function() { BuddySound.play("wake") })
                                    }
                                }
                            }

                            Column {
                                width: parent.width - Math.floor((parent.width - 14) * 0.54) - 14
                                spacing: 6

                                Row {
                                    width: parent.width
                                    Text {
                                        text: qsTr("Sound volume")
                                        color: Theme.textDim
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.DemiBold
                                    }
                                    Item { width: Math.max(4, parent.width - 115); height: 1 }
                                    Text {
                                        text: Math.round(panel.prefs.buddySfxVolume * 100) + "%"
                                        color: Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.DemiBold
                                    }
                                }

                                SizeSlider {
                                    width: parent.width
                                    height: 30
                                    from: 0.05
                                    to: 1.0
                                    value: panel.prefs.buddySfxVolume
                                    onMoved: (v) => panel.prefs.buddySfxVolume = Math.round(v * 100) / 100
                                }
                            }
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Solid color ring")
                            sub: qsTr("Show the solid accent outline and aura ring around buddy during interactions.")
                            checked: panel.prefs.buddySolidRing
                            onToggled: (c) => panel.prefs.buddySolidRing = c
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Show thinking")
                            sub: qsTr("Show what Buddy is doing while it works: the tool steps, the draft as it is written and its reasoning. Turn off for a quiet bubble that only shows the answer. Approvals and questions always show.")
                            checked: panel.prefs.buddyShowThinking
                            onToggled: (c) => panel.prefs.buddyShowThinking = c
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Real chat (AI provider)")
                            sub: panel.realChat
                                 ? qsTr("Questions go to %1 (%2). Local timers, app launch and plans still run directly.")
                                      .arg(panel.labelFor(panel.prefs.buddyProvider))
                                      .arg(panel.prefs.buddyModel || qsTr("no model"))
                                 : qsTr("Off: answers come from built-in local routes (timers, app launch, plans, sample cards).")
                            checked: panel.realChat
                            onToggled: (c) => panel.setRealChat(c)
                        }
                    }

                    SettingsSectionHeader {
                        title: qsTr("Ring colour")
                        rightText: panel.optionLabel(panel.ringOpts, SystemAppearance.ring)
                    }

                    SettingsGroupCard {
                        Text {
                            width: parent.width
                            text: qsTr("The neon halo your buddy wears when it's happy.")
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }
                        Flow {
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: panel.ringOpts
                                SettingsRingSwatch {
                                    required property var modelData
                                    option: modelData
                                    selected: SystemAppearance.ring === modelData.id
                                    onPicked: {
                                        SystemAppearance.ring = modelData.id
                                        panel.ringPreviewRequested()
                                    }
                                }
                            }
                        }
                    }

                    SettingsSectionHeader {
                        visible: panel.realChat
                        title: qsTr("Provider")
                        rightText: panel.labelFor(panel.prefs.buddyProvider)
                    }

                    SettingsGroupCard {
                        visible: panel.realChat

                        Column {
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Provider")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }

                            Segmented {
                                width: parent.width
                                height: 34
                                options: [
                                    { label: panel.labelFor("openai"), value: "openai" },
                                    { label: panel.labelFor("ollama"), value: "ollama" },
                                    { label: panel.labelFor("anthropic"), value: "anthropic" },
                                    { label: panel.labelFor("custom"), value: "custom" }
                                ]
                                value: panel.prefs.buddyProvider
                                onPicked: (v) => panel.pickProvider(v)
                            }
                        }

                        SettingsCardDivider {}

                        // Base URL and API key side by side when there is room.
                        Flow {
                            width: parent.width
                            spacing: 12

                            readonly property bool showKey: panel.showsKeyFor(panel.prefs.buddyProvider)
                            readonly property bool pair: width >= 520
                            readonly property real halfW: pair ? Math.floor((width - spacing) / 2) : width

                            SettingsFieldBox {
                                width: parent.showKey ? parent.halfW : parent.width
                                label: qsTr("Base URL")
                                placeholder: panel.infoFor(panel.prefs.buddyProvider).base
                                             || qsTr("https://your-endpoint")
                                hint: qsTr("POST %1").arg(panel.chatEndpoint())
                                mono: true
                                text: panel.prefs.buddyApiBase
                                onEdited: (v) => { panel.prefs.buddyApiBase = v; panel.markDirty() }
                            }

                            SettingsFieldBox {
                                visible: parent.showKey
                                width: parent.halfW
                                label: qsTr("API key")
                                placeholder: panel.infoFor(panel.prefs.buddyProvider).needsKey
                                             ? qsTr("Paste your API key")
                                             : qsTr("Optional for local servers")
                                hint: panel.infoFor(panel.prefs.buddyProvider).needsKey
                                      ? qsTr("Stored locally in Noch settings.")
                                      : qsTr("Leave blank if not required.")
                                secret: true
                                mono: true
                                text: panel.prefs.buddyApiKey
                                onEdited: (v) => { panel.prefs.buddyApiKey = v; panel.markDirty() }
                            }
                        }

                        SettingsCardDivider {}

                        // Model id beside the temperature slider.
                        Row {
                            width: parent.width
                            spacing: 14

                            SettingsFieldBox {
                                width: parent.width - 154
                                label: qsTr("Model")
                                placeholder: qsTr("Model id, e.g. gpt-4o-mini")
                                mono: true
                                text: panel.prefs.buddyModel
                                onEdited: (v) => { panel.prefs.buddyModel = v; panel.markDirty() }
                            }

                            SettingsLabeledSlider {
                                width: 140
                                label: qsTr("Temperature")
                                decimals: 2
                                value: panel.prefs.buddyTemperature
                                onMoved: (v) => panel.prefs.buddyTemperature = Math.round(v * 100) / 100
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: 6
                            visible: panel.modelChips.length > 0

                            Repeater {
                                model: panel.modelChips
                                delegate: SettingsChip {
                                    required property var modelData
                                    text: modelData
                                    selected: String(panel.prefs.buddyModel) === String(modelData)
                                    onPicked: panel.setModel(modelData)
                                }
                            }
                        }

                        SettingsCardDivider {}

                        Row {
                            width: parent.width
                            spacing: 12

                            SettingsPillButton {
                                id: testBtn
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "wifi"
                                label: panel.testState === "testing" ? qsTr("Testing…") : qsTr("Test connection")
                                primary: true
                                enabled: panel.testState !== "testing"
                                onClicked: panel.runTest()
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width - testBtn.width - 12)
                                text: panel.testMessage.length > 0
                                      ? panel.testMessage
                                      : qsTr("Verify endpoint & fetch available models.")
                                color: panel.testState === "ok" ? Theme.success
                                     : panel.testState === "failed" ? Theme.danger : Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontBase
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }

                    SettingsSectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Safety & permissions")
                        rightText: ({ "default": qsTr("Asks before side effects"),
                                      "acceptEdits": qsTr("Auto-approves workspace edits"),
                                      "plan": qsTr("Read-only"),
                                      "bypass": qsTr("Trusted — no prompts") })[panel.prefs.buddyPermissionMode] || ""
                    }

                    SettingsGroupCard {
                        visible: panel.prefs.buddyAgentMode

                        Column {
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Permission mode")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }

                            Segmented {
                                width: parent.width
                                height: 34
                                options: [
                                    { label: qsTr("Ask"), value: "default" },
                                    { label: qsTr("Auto-edit"), value: "acceptEdits" },
                                    { label: qsTr("Plan"), value: "plan" },
                                    { label: qsTr("Trusted"), value: "bypass" }
                                ]
                                value: panel.prefs.buddyPermissionMode
                                onPicked: (v) => panel.prefs.buddyPermissionMode = v
                            }

                            Text {
                                width: parent.width
                                text: panel.prefs.buddyPermissionMode === "bypass"
                                      ? qsTr("Every tool call runs without asking. Destructive commands, system folders and credential stores stay blocked.")
                                      : panel.prefs.buddyPermissionMode === "plan"
                                        ? qsTr("The agent can look around and read, but cannot run commands with side effects, edit files or control the computer.")
                                        : panel.prefs.buddyPermissionMode === "acceptEdits"
                                          ? qsTr("File edits inside the workspace run straight away. Commands and computer control still ask first.")
                                          : qsTr("Read-only commands run straight away. Anything that changes files, runs programs or clicks asks you first.")
                                color: panel.prefs.buddyPermissionMode === "bypass" ? Theme.warning : Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                        }

                        SettingsCardDivider {}

                        SettingsFieldBox {
                            width: parent.width
                            label: qsTr("Workspace folder")
                            placeholder: BuddyAgent ? BuddyAgent.effectiveWorkspace() : qsTr("~/Noch Workspace")
                            hint: qsTr("Auto-edit mode only auto-approves edits inside this folder.")
                            mono: true
                            text: panel.prefs.buddyWorkspaceRoot
                            onEdited: (v) => panel.prefs.buddyWorkspaceRoot = v
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Redact secrets")
                            sub: qsTr("Mask API keys, tokens and private keys in tool output before the model or the transcript sees them.")
                            checked: panel.prefs.buddyRedactSecrets
                            onToggled: (c) => panel.prefs.buddyRedactSecrets = c
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Keep run traces")
                            sub: qsTr("Save a local JSON trace of each run (model calls, tool calls, approvals) for debugging. Newest 40 are kept.")
                            checked: panel.prefs.buddyKeepTraces
                            onToggled: (c) => panel.prefs.buddyKeepTraces = c
                        }
                    }

                    SettingsSectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Agent behaviour")
                        rightText: qsTr("OpenAI Agents SDK patterns")
                    }

                    SettingsGroupCard {
                        visible: panel.prefs.buddyAgentMode

                        SettingsToggleRow {
                            label: qsTr("Stream replies")
                            sub: qsTr("Show the answer word by word while the model writes it (Anthropic and OpenAI-compatible providers).")
                            checked: panel.prefs.buddyStreamReplies
                            onToggled: (c) => panel.prefs.buddyStreamReplies = c
                        }

                        SettingsCardDivider {}

                        SettingsToggleRow {
                            label: qsTr("Specialist handoffs")
                            sub: qsTr("Let Buddy hand research to a web-only specialist and code work to a files-and-shell specialist, each with a smaller toolset.")
                            checked: panel.prefs.buddyHandoffs
                            onToggled: (c) => panel.prefs.buddyHandoffs = c
                        }
                    }
                }

                // Right: what the agent is allowed to do.
                Column {
                    width: panel.colW
                    spacing: 12

                    SettingsSectionHeader {
                        title: qsTr("Anthropic Agent SDK & Tools")
                        rightText: panel.prefs.buddyAgentMode
                                     ? qsTr("%1 of 9 tool suites armed")
                                         .arg((panel.prefs.buddyAllowBash ? 1 : 0)
                                              + (panel.prefs.buddyAllowComputer ? 1 : 0)
                                              + (panel.prefs.buddyAllowEditor ? 1 : 0)
                                              + (panel.prefs.buddyAllowCodeSearch ? 1 : 0)
                                              + (panel.prefs.buddyAllowMemory ? 1 : 0)
                                              + (panel.prefs.buddyAllowWebSearch ? 1 : 0)
                                              + (panel.prefs.buddyAllowToolSearch ? 1 : 0)
                                              + (panel.prefs.buddyAllowAdvisor ? 1 : 0)
                                              + (panel.prefs.buddyAllowTodoTask ? 1 : 0))
                                     : qsTr("Disabled")
                    }

                    SettingsGroupCard {
                        SettingsToggleRow {
                            label: qsTr("Enable Agent SDK tool loop")
                            sub: qsTr("Allow Anthropic and all configured AI agents to autonomously run multi-turn tool calls, execute commands, consult the advisor, and use the computer.")
                            checked: panel.prefs.buddyAgentMode
                            onToggled: (c) => panel.prefs.buddyAgentMode = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Command execution (bash / PowerShell)")
                            sub: qsTr("Run terminal and PowerShell commands asynchronously with output capture and 25s timeout protection.")
                            checked: panel.prefs.buddyAllowBash
                            onToggled: (c) => panel.prefs.buddyAllowBash = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Computer use (screen, mouse & keyboard)")
                            sub: qsTr("Claude-style computer use: screenshots and zoom scaled to your display, mouse moves, clicks, drags, scrolling and typing. Clicks and typing ask first; never types into password fields. Ctrl+Alt+Esc or holding Esc stops it, and touching the mouse or keyboard pauses it.")
                            checked: panel.prefs.buddyAllowComputer
                            onToggled: (c) => panel.prefs.buddyAllowComputer = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            label: qsTr("Use Claude's own computer tools")
                            sub: qsTr("The tools Claude was trained on: more reliable, several actions per step, sharper screenshots. No extra fee, but they add about 4,500 input tokens to each request while computer use is on (cached after the first). Older models fall back to the previous tools by themselves.")
                            checked: panel.prefs.buddyOfficialComputer
                            onToggled: (c) => panel.prefs.buddyOfficialComputer = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        Column {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Screenshot detail")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }
                            Segmented {
                                width: parent.width
                                height: 34
                                options: [
                                    { label: qsTr("Sharp"), value: "sharp" },
                                    { label: qsTr("Standard"), value: "standard" }
                                ]
                                value: panel.prefs.buddyComputerDetail
                                onPicked: (v) => panel.prefs.buddyComputerDetail = v
                            }
                            Text {
                                width: parent.width
                                text: panel.prefs.buddyComputerDetail === "standard"
                                      ? qsTr("Smaller screenshots: cheaper and faster, harder to read fine print.")
                                      : qsTr("Larger screenshots (up to 1920×1080): small text and icons are easier to read, at a few thousand more tokens per screenshot.")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            label: qsTr("Show Buddy's cursor")
                            sub: qsTr("Draw Buddy's own pointer gliding to each target, with click ripples, drag trails and a typing indicator. It never takes focus or blocks your own pointer.")
                            checked: panel.prefs.buddyCuShowCursor
                            onToggled: (c) => panel.prefs.buddyCuShowCursor = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            label: qsTr("Glow around the screen while in control")
                            sub: qsTr("A soft pulsing edge on every screen so it's always clear when Buddy is driving.")
                            checked: panel.prefs.buddyCuShowGlow
                            onToggled: (c) => panel.prefs.buddyCuShowGlow = c
                        }

                        SettingsLabeledSlider {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer && panel.prefs.buddyCuShowGlow
                            label: qsTr("Glow intensity")
                            decimals: 2
                            value: panel.prefs.buddyCuGlowIntensity
                            onMoved: (v) => panel.prefs.buddyCuGlowIntensity = Math.max(0.1, Math.round(v * 100) / 100)
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("File editor (str_replace_editor)")
                            sub: qsTr("Inspect directories, view file line ranges, create files, apply exact replacements, and undo edits.")
                            checked: panel.prefs.buddyAllowEditor
                            onToggled: (c) => panel.prefs.buddyAllowEditor = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Code search (glob_grep)")
                            sub: qsTr("Anthropic Agent SDK Glob & Grep: fast recursive file pattern matching and regex code search with line numbers.")
                            checked: panel.prefs.buddyAllowCodeSearch
                            onToggled: (c) => panel.prefs.buddyAllowCodeSearch = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Memory tool (/memories)")
                            sub: qsTr("Anthropic Memory Tool: persist notes, context, and structured files across conversations in a sandboxed /memories directory.")
                            checked: panel.prefs.buddyAllowMemory
                            onToggled: (c) => panel.prefs.buddyAllowMemory = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Web search & fetch (web_search / web_fetch)")
                            sub: qsTr("Search the live web with source citations and extract structured text, links, and metadata from URLs.")
                            checked: panel.prefs.buddyAllowWebSearch
                            onToggled: (c) => panel.prefs.buddyAllowWebSearch = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Tool search (tool_search BM25 & regex)")
                            sub: qsTr("Anthropic Tool Search: dynamically discover and inspect Agent SDK tool schemas using BM25 natural language or regex queries.")
                            checked: panel.prefs.buddyAllowToolSearch
                            onToggled: (c) => panel.prefs.buddyAllowToolSearch = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Advisor consultation (advisor_20260301)")
                            sub: qsTr("Anthropic Advisor Tool: invoke strategic architectural review, debugging verification, security auditing, and execution planning.")
                            checked: panel.prefs.buddyAllowAdvisor
                            onToggled: (c) => panel.prefs.buddyAllowAdvisor = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Task checklist & desktop context (todo_write)")
                            sub: qsTr("Anthropic TodoWrite live task tracker, Windows clipboard read/write, and live CPU/RAM/display telemetry.")
                            checked: panel.prefs.buddyAllowTodoTask
                            onToggled: (c) => panel.prefs.buddyAllowTodoTask = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Ask me questions")
                            sub: qsTr("When a request is unclear, Buddy can show a few choices to tap instead of guessing. An unanswered question is skipped after five minutes.")
                            checked: panel.prefs.buddyAllowAskUser
                            onToggled: (c) => panel.prefs.buddyAllowAskUser = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Screenshots in chat")
                            sub: qsTr("Buddy can show you a screenshot of your screen in the chat when you ask for one or want to see the result. The picture stays on this computer; Buddy is only told that it was shown. The newest 40 are kept in Noch's data folder.")
                            checked: panel.prefs.buddyAllowScreenshots
                            onToggled: (c) => panel.prefs.buddyAllowScreenshots = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyAgentMode }

                        Column {
                            visible: panel.prefs.buddyAgentMode
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Steps per task")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }
                            Segmented {
                                width: parent.width
                                height: 34
                                options: [
                                    { label: "20", value: 20 },
                                    { label: "40", value: 40 },
                                    { label: "70", value: 70 },
                                    { label: "100", value: 100 }
                                ]
                                value: panel.prefs.buddyMaxSteps
                                onPicked: (v) => panel.prefs.buddyMaxSteps = v
                            }
                            Text {
                                width: parent.width
                                text: qsTr("How many tool steps Buddy may take for one request before it stops and reports back. Repeating the same action over and over is stopped sooner.")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // ---- saved "always allow" rules ------------------------------
                    SettingsSectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Always-allow rules")
                        rightText: BuddyAgent && BuddyAgent.savedRules.length > 0
                                   ? qsTr("%n saved", "", BuddyAgent.savedRules.length) : ""
                    }

                    SettingsGroupCard {
                        visible: panel.prefs.buddyAgentMode

                        Text {
                            width: parent.width
                            text: BuddyAgent && BuddyAgent.savedRules.length > 0
                                  ? qsTr("These run without asking. Choosing “Always allow” on an approval adds a rule here; it covers only what it says. Hard stops, such as destructive commands and system folders, are never skipped.")
                                  : qsTr("Nothing saved yet. When Buddy asks first, “Always allow” can remember a narrow rule here, such as “run git status” or “edit files in this folder”.")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: BuddyAgent ? BuddyAgent.savedRules : []

                            Item {
                                id: ruleRow
                                required property var modelData
                                width: parent.width
                                implicitHeight: Math.max(34, ruleText.implicitHeight + 10)

                                Text {
                                    id: ruleText
                                    anchors.left: parent.left
                                    anchors.right: ruleRemove.left
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: ruleRow.modelData.label
                                    color: Theme.ink
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontBase
                                    font.weight: Font.Medium
                                    wrapMode: Text.WordWrap
                                }
                                SettingsPillButton {
                                    id: ruleRemove
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    label: qsTr("Remove")
                                    icon: "trash"
                                    onClicked: BuddyAgent.removeSavedRule(ruleRow.modelData.id)
                                }
                            }
                        }

                        SettingsPillButton {
                            visible: BuddyAgent && BuddyAgent.savedRules.length > 1
                            label: qsTr("Remove all")
                            danger: true
                            onClicked: BuddyAgent.clearSavedRules()
                        }
                    }

                    // ---- instruction files and slash commands --------------------
                    SettingsSectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Instructions & commands")
                    }

                    SettingsGroupCard {
                        id: instructionsCard
                        visible: panel.prefs.buddyAgentMode
                        property var found: ({ empty: true, sources: [] })
                        property string note: ""
                        function refresh() {
                            if (BuddyAgent)
                                found = BuddyAgent.projectInstructions()
                        }
                        Component.onCompleted: refresh()
                        // The workspace can change while Settings is open.
                        Connections {
                            target: panel.prefs
                            function onBuddyWorkspaceRootChanged() { instructionsCard.refresh() }
                        }

                        Text {
                            width: parent.width
                            text: instructionsCard.found.empty
                                  ? qsTr("Buddy is not reading any instruction file in this workspace.")
                                  : qsTr("Buddy reads, on every run:")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: instructionsCard.found.sources || []

                            Text {
                                required property var modelData
                                width: parent.width
                                text: "• " + modelData.name + (modelData.truncated ? qsTr(" (cut short)") : "")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            width: parent.width
                            text: qsTr("Put standing instructions in a NOCH.md in the workspace folder (AGENTS.md and CLAUDE.md work too). Your own commands are Markdown files in .noch/commands; type / in the chat to list them. Buddy always asks before changing one of these files.")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: instructionsCard.note.length > 0
                            width: parent.width
                            text: instructionsCard.note
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }

                        SettingsPillButton {
                            visible: instructionsCard.found.empty
                            label: qsTr("Create NOCH.md")
                            icon: "plus"
                            onClicked: {
                                instructionsCard.note = BuddyAgent.initProjectInstructions().message
                                instructionsCard.refresh()
                            }
                        }
                    }

                    // ---- push-to-talk voice --------------------------------------
                    SettingsSectionHeader {
                        title: qsTr("Voice input")
                        rightText: Voice.enabled ? (Voice.configured ? qsTr("Ready") : qsTr("Needs setup")) : qsTr("Off")
                    }

                    SettingsGroupCard {
                        SettingsToggleRow {
                            label: qsTr("Push-to-talk")
                            sub: qsTr("Hold the microphone button in Buddy's bubble (or the shortcut) and speak; release to turn it into text. Audio is recorded only while you hold and turned into text by a model on this computer. It is kept in memory only, never sent anywhere and never stored.")
                            checked: panel.prefs.buddyVoiceEnabled
                            onToggled: (c) => panel.prefs.buddyVoiceEnabled = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyVoiceEnabled }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyVoiceEnabled
                            label: qsTr("Shortcut: hold %1").arg(TalkKey.shortcut.length > 0 ? TalkKey.shortcut : "Ctrl+Alt+V")
                            sub: TalkKey.registered || !panel.prefs.buddyVoiceHotkey
                                 ? qsTr("Talk without clicking, from any app.")
                                 : qsTr("Another app is using this shortcut, or this system does not support it. The microphone button still works.")
                            checked: panel.prefs.buddyVoiceHotkey
                            onToggled: (c) => panel.prefs.buddyVoiceHotkey = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyVoiceEnabled }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyVoiceEnabled
                            label: qsTr("Send right away")
                            sub: qsTr("Send what you said without waiting for you to check it first.")
                            checked: panel.prefs.buddyVoiceAutoSend
                            onToggled: (c) => panel.prefs.buddyVoiceAutoSend = c
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyVoiceEnabled }

                        Column {
                            visible: panel.prefs.buddyVoiceEnabled
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Speech to text: %1, on this computer").arg(Voice.engine)
                                color: Theme.ink
                                font.family: Theme.font
                                font.pixelSize: Theme.fontBase
                                font.weight: Font.Medium
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                width: parent.width
                                text: !Voice.configured ? Voice.problem
                                      : Voice.ready ? qsTr("Ready. English only. Nothing you say leaves this computer.")
                                      : qsTr("Starting the speech model…")
                                color: Voice.configured ? Theme.textDim : Theme.warning
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                SettingsPillButton {
                                    icon: "folder"
                                    label: qsTr("Open models folder")
                                    onClicked: Voice.openModelFolder()
                                }
                            }
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyVoiceEnabled }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyVoiceEnabled
                            label: qsTr("Wake word")
                            sub: Wake.detector === "speech"
                                 ? qsTr("Say the phrase below and Buddy starts listening, then does what you ask and reports back. The microphone stays open while this is on (Windows shows its microphone icon). Whatever is said near it is turned into text on this computer, compared with the phrase and forgotten.")
                                 : qsTr("Say “Hey Buddy” and Buddy starts listening, then does what you ask and reports back. The phrase and your request are both recognised on this computer: nothing is sent anywhere. The microphone stays open while this is on (Windows shows its microphone icon).")
                            checked: panel.prefs.buddyWakeEnabled
                            onToggled: (c) => panel.prefs.buddyWakeEnabled = c
                        }

                        SettingsLabeledSlider {
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddyWakeEnabled && Wake.detector !== "speech"
                            label: qsTr("Sensitivity")
                            decimals: 2
                            // Left: strict (fewer false wake-ups). Right: easy (wakes on a quieter or less clear “Hey Buddy”).
                            value: (0.9 - panel.prefs.buddyWakeThreshold) / 0.7
                            onMoved: (v) => panel.prefs.buddyWakeThreshold = Math.round((0.9 - 0.7 * v) * 100) / 100
                        }

                        Flow {
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddyWakeEnabled && Wake.detector !== "speech"
                            width: parent.width
                            spacing: 8
                            SettingsPillButton {
                                icon: "folder"
                                label: qsTr("Open models folder")
                                onClicked: Wake.openModelFolder()
                            }
                        }

                        SettingsFieldBox {
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddyWakeEnabled && Wake.detector === "speech"
                            width: parent.width
                            label: qsTr("Wake phrase")
                            placeholder: "hey buddy"
                            hint: qsTr("Two or more syllables work best; used at the start of a sentence.")
                            text: panel.prefs.buddyWakePhrase
                            onEdited: (v) => panel.prefs.buddyWakePhrase = v
                        }

                        Text {
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddyWakeEnabled
                            width: parent.width
                            text: Wake.blocked.length > 0 ? Wake.blocked
                                  : Wake.error.length > 0 ? Wake.error
                                  : Wake.state === "loading" ? qsTr("Starting the local detector…")
                                  : Wake.state === "listening" ? (Wake.detector === "local"
                                        ? qsTr("Listening for “Hey Buddy” on this computer.")
                                        : qsTr("Listening for “%1”.").arg(panel.prefs.buddyWakePhrase))
                                  : Wake.state === "awaiting" ? qsTr("Heard it. Listening for your request…")
                                  : Wake.state === "paused" ? qsTr("Paused for a moment.") : ""
                            color: Wake.blocked.length > 0 || Wake.error.length > 0 ? Theme.warning : Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            // The local detector could not start: say why, and that the speech service is checking instead.
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddyWakeEnabled && Wake.localProblem.length > 0
                            width: parent.width
                            text: qsTr("Local detector unavailable: %1").arg(Wake.localProblem)
                            color: Theme.warning
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }

                        SettingsCardDivider { visible: panel.prefs.buddyVoiceEnabled }

                        SettingsToggleRow {
                            visible: panel.prefs.buddyVoiceEnabled
                            label: qsTr("Read answers aloud")
                            sub: qsTr("When a request started by voice finishes, needs your approval or fails, Buddy says a short summary out loud. Never code or links.")
                            checked: panel.prefs.buddySpeakReports
                            onToggled: (c) => panel.prefs.buddySpeakReports = c
                        }

                        Column {
                            visible: panel.prefs.buddyVoiceEnabled && panel.prefs.buddySpeakReports
                            width: parent.width
                            spacing: 10

                            SettingsToggleRow {
                                label: qsTr("Read every answer")
                                sub: qsTr("Also read answers to requests you typed.")
                                checked: panel.prefs.buddyTtsSpeakAll
                                onToggled: (c) => panel.prefs.buddyTtsSpeakAll = c
                            }

                            Text {
                                text: qsTr("Voice")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                Repeater {
                                    model: [
                                        { id: "system", name: qsTr("This computer") },
                                        { id: "google", name: qsTr("Google Cloud") },
                                        { id: "kokoro", name: qsTr("Kokoro") }
                                    ]
                                    delegate: SettingsChip {
                                        required property var modelData
                                        text: modelData.name
                                        selected: panel.prefs.buddyTtsEngine === modelData.id
                                        onPicked: panel.prefs.buddyTtsEngine = modelData.id
                                    }
                                }
                            }

                            Text {
                                visible: panel.prefs.buddyTtsEngine === "system"
                                width: parent.width
                                text: Speech.available
                                      ? qsTr("Uses the voice built into this computer. Nothing leaves it.")
                                      : Speech.problem
                                color: Speech.available ? Theme.textDim : Theme.warning
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }

                            Column {
                                visible: panel.prefs.buddyTtsEngine === "google"
                                width: parent.width
                                spacing: 10
                                SettingsFieldBox {
                                    width: parent.width
                                    label: qsTr("Google Cloud API key")
                                    placeholder: qsTr("Paste your API key")
                                    hint: qsTr("Needs the Cloud Text-to-Speech API enabled. The spoken text is sent to Google. Stored locally in Noch settings.")
                                    secret: true
                                    mono: true
                                    text: panel.prefs.buddyTtsGoogleKey
                                    onEdited: (v) => panel.prefs.buddyTtsGoogleKey = v
                                }
                                SettingsFieldBox {
                                    width: parent.width
                                    label: qsTr("Voice")
                                    placeholder: "en-US-Neural2-F"
                                    hint: qsTr("Any Google voice name, such as en-US-Studio-O or en-GB-Neural2-A.")
                                    mono: true
                                    text: panel.prefs.buddyTtsGoogleVoice
                                    onEdited: (v) => panel.prefs.buddyTtsGoogleVoice = v
                                }
                            }

                            Column {
                                visible: panel.prefs.buddyTtsEngine === "kokoro"
                                width: parent.width
                                spacing: 10
                                SettingsFieldBox {
                                    width: parent.width
                                    label: qsTr("Kokoro server")
                                    placeholder: "http://127.0.0.1:8880"
                                    hint: qsTr("A Kokoro TTS server running on this computer (for example Kokoro-FastAPI). Speech is only sent to this computer or over https.")
                                    mono: true
                                    text: panel.prefs.buddyTtsKokoroBase
                                    onEdited: (v) => panel.prefs.buddyTtsKokoroBase = v
                                }
                                SettingsFieldBox {
                                    width: parent.width
                                    label: qsTr("Voice")
                                    placeholder: "af_heart"
                                    mono: true
                                    text: panel.prefs.buddyTtsKokoroVoice
                                    onEdited: (v) => panel.prefs.buddyTtsKokoroVoice = v
                                }
                            }

                            SettingsLabeledSlider {
                                visible: panel.prefs.buddyTtsEngine !== "system"
                                label: qsTr("Speed")
                                decimals: 2
                                // 0.7 .. 1.5
                                value: (panel.prefs.buddyTtsSpeed - 0.7) / 0.8
                                onMoved: (v) => panel.prefs.buddyTtsSpeed = Math.round((0.7 + 0.8 * v) * 100) / 100
                            }

                            Text {
                                visible: panel.prefs.buddyTtsEngine !== "system" && !Speech.available
                                width: parent.width
                                text: Speech.problem
                                color: Theme.warning
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                visible: Speech.error.length > 0
                                width: parent.width
                                text: Speech.error
                                color: Theme.warning
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                SettingsPillButton {
                                    icon: "volume"
                                    label: Speech.speaking ? qsTr("Stop") : qsTr("Test voice")
                                    onClicked: Speech.speaking ? Speech.stop() : Speech.test()
                                }
                            }
                        }
                    }

                }
            }
        }
    }

    Rectangle {
        id: bottomRule
        anchors.top: content.bottom
        width: parent.width
        height: 1
        color: Theme.borderSoft
    }

    // ---------- footer --------------------------------------------------------
    Item {
        id: footer
        anchors.top: bottomRule.bottom
        width: parent.width
        height: panel.footerH

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Text {
                text: "Noch"
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontMd
                font.weight: Font.Bold
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: verText.implicitWidth + 12
                height: 20
                radius: 10
                color: Theme.raised
                border.width: 0
                border.color: Theme.border
                Text {
                    id: verText
                    anchors.centerIn: parent
                    text: "v" + panel.version
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SettingsPillButton {
                icon: "folder"
                label: qsTr("Data folder")
                onClicked: Dock.openDataFolder()
            }

            SettingsPillButton {
                icon: "power"
                label: qsTr("Quit Noch")
                danger: true
                onClicked: panel.quitRequested()
            }
        }
    }
}
