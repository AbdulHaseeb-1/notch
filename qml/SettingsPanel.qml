pragma ComponentBehavior: Bound
import QtQuick

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
    component SectionHeader: Item {
        property string title
        property string rightText: ""
        width: parent ? parent.width : 300
        height: 20

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: parent.title.toUpperCase()
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }
        Text {
            visible: parent.rightText.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: parent.rightText
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    component GroupCard: Rectangle {
        default property alias cardContent: innerCol.data
        width: parent ? parent.width : 340
        implicitHeight: innerCol.implicitHeight + 24
        radius: 14
        color: Theme.raised
        border.width: 1
        border.color: Theme.borderSoft

        Column {
            id: innerCol
            x: 16
            y: 12
            width: parent.width - 32
            spacing: 10
        }
    }

    component CardDivider: Rectangle {
        width: parent ? parent.width : 300
        height: 1
        color: Theme.borderSoft
    }

    component ToggleRow: Item {
        id: tr
        property string label
        property string sub: ""
        property bool checked
        property bool enabled2: true
        signal toggled(bool checked)

        width: parent ? parent.width : 300
        implicitHeight: Math.max(36, textCol.implicitHeight + 6)

        Column {
            id: textCol
            anchors.left: parent.left
            anchors.right: sw.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            opacity: tr.enabled2 ? 1 : 0.45

            Text {
                text: tr.label
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                visible: tr.sub.length > 0
                width: parent.width
                text: tr.sub
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }
        }

        Toggle {
            id: sw
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: tr.checked
            enabled2: tr.enabled2
            onToggled: (c) => tr.toggled(c)
        }
    }

    component IconButton: Item {
        id: ib
        property string icon
        property real glyphSize: 14
        property bool danger: false
        signal clicked()
        width: 26
        height: 26

        Rectangle {
            anchors.fill: parent
            radius: 7
            color: ibHit.containsMouse ? (ib.danger ? Theme.dangerSoft : Theme.tile) : "transparent"
        }
        Glyph {
            anchors.centerIn: parent
            name: ib.icon
            size: ib.glyphSize
            color: ib.danger && ibHit.containsMouse ? Theme.danger
                 : ibHit.containsMouse ? Theme.ink : Theme.textDim
            scale: ibHit.pressed ? 0.86 : 1
            Behavior on scale { NumberAnimation { duration: 100 } }
        }
        MouseArea {
            id: ibHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ib.clicked()
        }
    }

    component SmallPillButton: Item {
        id: spb
        property string label
        property string icon: ""
        property bool primary: false
        property bool danger: false
        signal clicked()
        implicitWidth: spbRow.implicitWidth + 22
        implicitHeight: 28
        opacity: spb.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: spb.primary ? (spbHit.containsMouse ? Theme.accentHover : Theme.accent)
                 : spb.danger ? (spbHit.containsMouse ? Theme.dangerSoft : "transparent")
                 : (spbHit.containsMouse ? Theme.tileActive : Theme.tile)
            border.width: spb.primary ? 0 : 1
            border.color: spb.danger && spbHit.containsMouse ? Theme.danger : Theme.border
            scale: spbHit.pressed ? 0.95 : 1
            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
        Row {
            id: spbRow
            anchors.centerIn: parent
            spacing: 5
            Glyph {
                visible: spb.icon.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: spb.icon
                size: 12
                stroke: 2.1
                color: spb.primary ? Theme.inverse : (spb.danger ? Theme.danger : Theme.ink)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: spb.label
                color: spb.primary ? Theme.inverse : (spb.danger ? Theme.danger : Theme.ink)
                font.family: Theme.font
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }
        MouseArea {
            id: spbHit
            anchors.fill: parent
            hoverEnabled: true
            enabled: spb.enabled
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: spb.label
            onClicked: spb.clicked()
        }
    }

    component FieldBox: Item {
        id: fb
        property string label: ""
        property string hint: ""
        property string placeholder: ""
        property string text: ""
        property bool secret: false
        property bool mono: false
        property bool clearable: true
        property bool revealed: false
        signal edited(string text)
        signal accepted()

        readonly property bool hasText: text.length > 0

        width: parent ? parent.width : 260
        implicitHeight: (label.length > 0 ? 21 : 0) + 36 + (hint.length > 0 ? 19 : 0)

        Text {
            id: fbLabel
            visible: fb.label.length > 0
            anchors.top: parent.top
            width: parent.width
            height: 15
            text: fb.label
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: fbBox
            anchors.top: fbLabel.visible ? fbLabel.bottom : parent.top
            anchors.topMargin: fbLabel.visible ? 6 : 0
            width: parent.width
            height: 36
            radius: 10
            color: Theme.field
            border.width: 1
            border.color: fbInput.activeFocus ? Theme.accent : Theme.fieldBorder
            Behavior on border.color { ColorAnimation { duration: 130 } }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                onClicked: fbInput.forceActiveFocus()
            }
        }

        Row {
            id: fbTail
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: fbBox.verticalCenter
            spacing: 1

            Item {
                id: fbClear
                visible: fb.clearable && fb.hasText
                width: 22
                height: 22
                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 10
                    stroke: 2.2
                    color: fbClearHit.containsMouse ? Theme.text : Theme.textFaint
                }
                MouseArea {
                    id: fbClearHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Clear")
                    onClicked: {
                        fbInput.text = ""
                        fb.edited("")
                        fbInput.forceActiveFocus()
                    }
                }
            }

            Item {
                visible: fb.secret
                width: 22
                height: 22
                Glyph {
                    anchors.centerIn: parent
                    name: fb.revealed ? "eye-off" : "eye"
                    size: 13
                    stroke: 1.7
                    color: fbRevealHit.containsMouse ? Theme.text : Theme.textFaint
                }
                MouseArea {
                    id: fbRevealHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: fb.revealed ? qsTr("Hide value") : qsTr("Show value")
                    onClicked: fb.revealed = !fb.revealed
                }
            }
        }

        TextInput {
            id: fbInput
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: fbTail.left
            anchors.rightMargin: 5
            anchors.verticalCenter: fbBox.verticalCenter
            height: 20
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.ink
            selectionColor: Theme.accent
            selectedTextColor: Theme.inverse
            font.family: fb.mono ? "Consolas" : Theme.font
            font.pixelSize: 13
            clip: true
            selectByMouse: true
            echoMode: fb.secret && !fb.revealed ? TextInput.Password : TextInput.Normal
            text: fb.text
            onTextEdited: fb.edited(text)
            Keys.onReturnPressed: (event) => { fb.accepted(); event.accepted = true }
            Keys.onEnterPressed: (event) => { fb.accepted(); event.accepted = true }

            Accessible.role: Accessible.EditableText
            Accessible.name: fb.label.length > 0 ? fb.label : fb.placeholder

            Text {
                anchors.fill: parent
                visible: fbInput.text.length === 0
                verticalAlignment: TextInput.AlignVCenter
                text: fb.placeholder
                color: Theme.textFaint
                font: fbInput.font
                elide: Text.ElideRight
            }
        }

        Text {
            anchors.top: fbBox.bottom
            anchors.topMargin: 6
            visible: fb.hint.length > 0
            width: parent.width
            text: fb.hint
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
    }

    component Chip: Item {
        id: chip
        property string text: ""
        property bool selected: false
        signal picked()
        implicitWidth: chipText.implicitWidth + 20
        implicitHeight: 26

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: chip.selected ? Theme.accent
                 : chipHit.containsMouse ? Theme.tileActive : Theme.tile
            border.width: chip.selected ? 0 : 1
            border.color: Theme.border
            Behavior on color { ColorAnimation { duration: 120 } }
            scale: chipHit.pressed ? 0.96 : 1
            Behavior on scale { NumberAnimation { duration: 100 } }
        }
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            color: chip.selected ? Theme.inverse : (chipHit.containsMouse ? Theme.ink : Theme.textDim)
            font.family: Theme.font
            font.pixelSize: 11
            font.weight: chip.selected ? Font.DemiBold : Font.Medium
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        MouseArea {
            id: chipHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: chip.text
            onClicked: chip.picked()
        }
    }

    component LabeledSlider: Column {
        id: ls
        property string label: ""
        property real value: 0
        property int decimals: 2
        signal moved(real value)
        width: parent ? parent.width : 200
        spacing: 6

        Item {
            width: parent.width
            height: 15
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: ls.label
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: Number(ls.value).toFixed(ls.decimals)
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.Bold
                font.features: ({ "tnum": 1 })
            }
        }
        SizeSlider {
            width: parent.width
            height: 36
            from: 0
            to: 1
            value: ls.value
            onMoved: (v) => ls.moved(v)
        }
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
                font.pixelSize: 16
                font.weight: Font.Bold
            }
            Text {
                text: panel.tab === 0 ? qsTr("Size, notch & system behavior")
                    : (panel.tab === 1 ? qsTr("Manage pinned dock & installed apps")
                                       : qsTr("Buddy, AI provider & agent tools"))
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: 11
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

        IconButton {
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

                    SectionHeader {
                        title: qsTr("Size & scale")
                        rightText: Math.round(panel.prefs.sizeScale * 100) + "%"
                    }

                    GroupCard {
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

                    SectionHeader { title: qsTr("Notch") }

                    GroupCard {
                        ToggleRow {
                            label: qsTr("Monochrome app icons")
                            sub: qsTr("Render dock icons in quiet monochrome that sits well on dark stone.")
                            checked: panel.prefs.monoIcons
                            onToggled: (c) => panel.prefs.monoIcons = c
                        }
                        CardDivider {}
                        ToggleRow {
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

                    SectionHeader { title: qsTr("Behavior & system") }

                    GroupCard {
                        ToggleRow {
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
                                font.pixelSize: 12
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

                        CardDivider {}
                        ToggleRow {
                            label: qsTr("Quiet in fullscreen")
                            sub: qsTr("Ignore top-edge hover while a game or fullscreen video is active.")
                            checked: panel.prefs.suppressInFullscreen
                            onToggled: (c) => panel.prefs.suppressInFullscreen = c
                        }
                        CardDivider {}
                        Item {
                            width: parent.width
                            implicitHeight: buddyToggle.implicitHeight

                            ToggleRow {
                                id: buddyToggle
                                width: parent.width - (resetPosBtn.visible ? resetPosBtn.width + 12 : 0)
                                label: qsTr("Desktop buddy")
                                sub: qsTr("Interactive orb companion. Drag it anywhere or click it for tasks, timers and AI chat.")
                                checked: panel.prefs.buddyEnabled
                                onToggled: (c) => panel.prefs.buddyEnabled = c
                            }
                            SmallPillButton {
                                id: resetPosBtn
                                visible: panel.buddyMovedCustom
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "reset"
                                label: qsTr("Reset position")
                                onClicked: panel.resetBuddyPosRequested()
                            }
                        }
                        CardDivider {}
                        ToggleRow {
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
                        CardDivider { visible: panel.startupSupported }
                        ToggleRow {
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
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            SmallPillButton {
                                icon: "reset"
                                label: qsTr("Defaults")
                                onClicked: panel.resetRequested()
                            }
                            SmallPillButton {
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
                        border.width: 1
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
                            font.pixelSize: 12
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
                                        border.width: 1
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
                                        font.pixelSize: 13
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

                                    IconButton {
                                        icon: "up"
                                        glyphSize: 13
                                        visible: appRow.index > 0
                                        onClicked: panel.moveRequested(appRow.index, appRow.index - 1)
                                    }
                                    IconButton {
                                        icon: "down"
                                        glyphSize: 13
                                        visible: appRow.index < panel.dockModel.length - 1
                                        onClicked: panel.moveRequested(appRow.index, appRow.index + 1)
                                    }
                                    IconButton {
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
                            font.pixelSize: 11
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
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: panel.appsListH
                        radius: 14
                        color: Theme.raised
                        border.width: 1
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
                                font.pixelSize: 13
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

                            IconButton {
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
                            font.pixelSize: 12
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
                                        border.width: 1
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
                                    font.pixelSize: 13
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
                                            font.pixelSize: 12
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

                    SectionHeader {
                        title: qsTr("Desktop buddy")
                        rightText: panel.prefs.buddyEnabled
                                     ? (panel.realChat ? panel.labelFor(panel.prefs.buddyProvider) : qsTr("Local mode"))
                                     : qsTr("Disabled")
                    }

                    GroupCard {
                        Item {
                            width: parent.width
                            implicitHeight: enableBuddyRow.implicitHeight

                            ToggleRow {
                                id: enableBuddyRow
                                width: parent.width - (resetBuddyBtn2.visible ? resetBuddyBtn2.width + 12 : 0)
                                label: qsTr("Enable desktop buddy")
                                sub: qsTr("Show the interactive orb on your desktop. Drag to move, click to ask.")
                                checked: panel.prefs.buddyEnabled
                                onToggled: (c) => panel.prefs.buddyEnabled = c
                            }

                            SmallPillButton {
                                id: resetBuddyBtn2
                                visible: panel.buddyMovedCustom
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon: "reset"
                                label: qsTr("Reset position")
                                onClicked: panel.resetBuddyPosRequested()
                            }
                        }

                        CardDivider {}

                        Item {
                            width: parent.width
                            implicitHeight: sfxToggleRow.implicitHeight

                            ToggleRow {
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

                            SmallPillButton {
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
                                    font.pixelSize: 11
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
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                    Item { width: Math.max(4, parent.width - 115); height: 1 }
                                    Text {
                                        text: Math.round(panel.prefs.buddySfxVolume * 100) + "%"
                                        color: Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: 11
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

                        CardDivider {}

                        ToggleRow {
                            label: qsTr("Solid color ring")
                            sub: qsTr("Show the solid accent outline and aura ring around buddy during interactions.")
                            checked: panel.prefs.buddySolidRing
                            onToggled: (c) => panel.prefs.buddySolidRing = c
                        }

                        CardDivider {}

                        ToggleRow {
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

                    SectionHeader {
                        visible: panel.realChat
                        title: qsTr("Provider")
                        rightText: panel.labelFor(panel.prefs.buddyProvider)
                    }

                    GroupCard {
                        visible: panel.realChat

                        Column {
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Provider")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: 11
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

                        CardDivider {}

                        // Base URL and API key side by side when there is room.
                        Flow {
                            width: parent.width
                            spacing: 12

                            readonly property bool showKey: panel.showsKeyFor(panel.prefs.buddyProvider)
                            readonly property bool pair: width >= 520
                            readonly property real halfW: pair ? Math.floor((width - spacing) / 2) : width

                            FieldBox {
                                width: parent.showKey ? parent.halfW : parent.width
                                label: qsTr("Base URL")
                                placeholder: panel.infoFor(panel.prefs.buddyProvider).base
                                             || qsTr("https://your-endpoint")
                                hint: qsTr("POST %1").arg(panel.chatEndpoint())
                                mono: true
                                text: panel.prefs.buddyApiBase
                                onEdited: (v) => { panel.prefs.buddyApiBase = v; panel.markDirty() }
                            }

                            FieldBox {
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

                        CardDivider {}

                        // Model id beside the temperature slider.
                        Row {
                            width: parent.width
                            spacing: 14

                            FieldBox {
                                width: parent.width - 154
                                label: qsTr("Model")
                                placeholder: qsTr("Model id, e.g. gpt-4o-mini")
                                mono: true
                                text: panel.prefs.buddyModel
                                onEdited: (v) => { panel.prefs.buddyModel = v; panel.markDirty() }
                            }

                            LabeledSlider {
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
                                delegate: Chip {
                                    required property var modelData
                                    text: modelData
                                    selected: String(panel.prefs.buddyModel) === String(modelData)
                                    onPicked: panel.setModel(modelData)
                                }
                            }
                        }

                        CardDivider {}

                        Row {
                            width: parent.width
                            spacing: 12

                            SmallPillButton {
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
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }

                    SectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Safety & permissions")
                        rightText: ({ "default": qsTr("Asks before side effects"),
                                      "acceptEdits": qsTr("Auto-approves workspace edits"),
                                      "plan": qsTr("Read-only"),
                                      "bypass": qsTr("Trusted — no prompts") })[panel.prefs.buddyPermissionMode] || ""
                    }

                    GroupCard {
                        visible: panel.prefs.buddyAgentMode

                        Column {
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width
                                text: qsTr("Permission mode")
                                color: Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: 11
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
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }

                        CardDivider {}

                        FieldBox {
                            width: parent.width
                            label: qsTr("Workspace folder")
                            placeholder: BuddyAgent ? BuddyAgent.effectiveWorkspace() : qsTr("~/Noch Workspace")
                            hint: qsTr("Auto-edit mode only auto-approves edits inside this folder.")
                            mono: true
                            text: panel.prefs.buddyWorkspaceRoot
                            onEdited: (v) => panel.prefs.buddyWorkspaceRoot = v
                        }

                        CardDivider {}

                        ToggleRow {
                            label: qsTr("Redact secrets")
                            sub: qsTr("Mask API keys, tokens and private keys in tool output before the model or the transcript sees them.")
                            checked: panel.prefs.buddyRedactSecrets
                            onToggled: (c) => panel.prefs.buddyRedactSecrets = c
                        }

                        CardDivider {}

                        ToggleRow {
                            label: qsTr("Keep run traces")
                            sub: qsTr("Save a local JSON trace of each run (model calls, tool calls, approvals) for debugging. Newest 40 are kept.")
                            checked: panel.prefs.buddyKeepTraces
                            onToggled: (c) => panel.prefs.buddyKeepTraces = c
                        }
                    }

                    SectionHeader {
                        visible: panel.prefs.buddyAgentMode
                        title: qsTr("Agent behaviour")
                        rightText: qsTr("OpenAI Agents SDK patterns")
                    }

                    GroupCard {
                        visible: panel.prefs.buddyAgentMode

                        ToggleRow {
                            label: qsTr("Stream replies")
                            sub: qsTr("Show the answer word by word while the model writes it (Anthropic and OpenAI-compatible providers).")
                            checked: panel.prefs.buddyStreamReplies
                            onToggled: (c) => panel.prefs.buddyStreamReplies = c
                        }

                        CardDivider {}

                        ToggleRow {
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

                    SectionHeader {
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

                    GroupCard {
                        ToggleRow {
                            label: qsTr("Enable Agent SDK tool loop")
                            sub: qsTr("Allow Anthropic and all configured AI agents to autonomously run multi-turn tool calls, execute commands, consult the advisor, and use the computer.")
                            checked: panel.prefs.buddyAgentMode
                            onToggled: (c) => panel.prefs.buddyAgentMode = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Command execution (bash / PowerShell)")
                            sub: qsTr("Run terminal and PowerShell commands asynchronously with output capture and 25s timeout protection.")
                            checked: panel.prefs.buddyAllowBash
                            onToggled: (c) => panel.prefs.buddyAllowBash = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Computer use (screen, mouse & keyboard)")
                            sub: qsTr("Claude-style computer use: screenshots and zoom scaled to your display, mouse moves, clicks, drags, scrolling and typing. Clicks and typing ask first; never types into password fields. Ctrl+Alt+Esc or holding Esc stops it, and touching the mouse or keyboard pauses it.")
                            checked: panel.prefs.buddyAllowComputer
                            onToggled: (c) => panel.prefs.buddyAllowComputer = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            label: qsTr("Show Buddy's cursor")
                            sub: qsTr("Draw Buddy's own pointer gliding to each target, with click ripples, drag trails and a typing indicator. It never takes focus or blocks your own pointer.")
                            checked: panel.prefs.buddyCuShowCursor
                            onToggled: (c) => panel.prefs.buddyCuShowCursor = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer
                            label: qsTr("Glow around the screen while in control")
                            sub: qsTr("A soft pulsing edge on every screen so it's always clear when Buddy is driving.")
                            checked: panel.prefs.buddyCuShowGlow
                            onToggled: (c) => panel.prefs.buddyCuShowGlow = c
                        }

                        LabeledSlider {
                            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowComputer && panel.prefs.buddyCuShowGlow
                            label: qsTr("Glow intensity")
                            decimals: 2
                            value: panel.prefs.buddyCuGlowIntensity
                            onMoved: (v) => panel.prefs.buddyCuGlowIntensity = Math.max(0.1, Math.round(v * 100) / 100)
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("File editor (str_replace_editor)")
                            sub: qsTr("Inspect directories, view file line ranges, create files, apply exact replacements, and undo edits.")
                            checked: panel.prefs.buddyAllowEditor
                            onToggled: (c) => panel.prefs.buddyAllowEditor = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Code search (glob_grep)")
                            sub: qsTr("Anthropic Agent SDK Glob & Grep: fast recursive file pattern matching and regex code search with line numbers.")
                            checked: panel.prefs.buddyAllowCodeSearch
                            onToggled: (c) => panel.prefs.buddyAllowCodeSearch = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Memory tool (/memories)")
                            sub: qsTr("Anthropic Memory Tool: persist notes, context, and structured files across conversations in a sandboxed /memories directory.")
                            checked: panel.prefs.buddyAllowMemory
                            onToggled: (c) => panel.prefs.buddyAllowMemory = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Web search & fetch (web_search / web_fetch)")
                            sub: qsTr("Search the live web with source citations and extract structured text, links, and metadata from URLs.")
                            checked: panel.prefs.buddyAllowWebSearch
                            onToggled: (c) => panel.prefs.buddyAllowWebSearch = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Tool search (tool_search BM25 & regex)")
                            sub: qsTr("Anthropic Tool Search: dynamically discover and inspect Agent SDK tool schemas using BM25 natural language or regex queries.")
                            checked: panel.prefs.buddyAllowToolSearch
                            onToggled: (c) => panel.prefs.buddyAllowToolSearch = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Advisor consultation (advisor_20260301)")
                            sub: qsTr("Anthropic Advisor Tool: invoke strategic architectural review, debugging verification, security auditing, and execution planning.")
                            checked: panel.prefs.buddyAllowAdvisor
                            onToggled: (c) => panel.prefs.buddyAllowAdvisor = c
                        }

                        CardDivider { visible: panel.prefs.buddyAgentMode }

                        ToggleRow {
                            visible: panel.prefs.buddyAgentMode
                            label: qsTr("Task checklist & desktop context (todo_write)")
                            sub: qsTr("Anthropic TodoWrite live task tracker, Windows clipboard read/write, and live CPU/RAM/display telemetry.")
                            checked: panel.prefs.buddyAllowTodoTask
                            onToggled: (c) => panel.prefs.buddyAllowTodoTask = c
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
                font.pixelSize: 13
                font.weight: Font.ExtraBold
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: verText.implicitWidth + 12
                height: 20
                radius: 10
                color: Theme.raised
                border.width: 1
                border.color: Theme.border
                Text {
                    id: verText
                    anchors.centerIn: parent
                    text: "v" + panel.version
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SmallPillButton {
                icon: "folder"
                label: qsTr("Data folder")
                onClicked: Dock.openDataFolder()
            }

            SmallPillButton {
                icon: "power"
                label: qsTr("Quit Noch")
                danger: true
                onClicked: panel.quitRequested()
            }
        }
    }
}
