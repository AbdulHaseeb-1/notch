pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// The Settings panel under the notch: a sidebar of categories on the left and
// one page at a time on the right (see settings/pages/*Page.qml). Every page is
// a column of sections, each section a card of controls. The sidebar has a
// search box that narrows the categories by what they contain.
Item {
    id: settings

    property var prefs
    property bool startupSupported: false
    property bool launchAtStartup: false
    property real maxPanelWidth: 620
    property real maxBodyHeight: 100000
    property string version: ""

    signal closeRequested()
    signal resetRequested()
    signal quitRequested()
    signal startupToggled(bool enabled)
    signal resetBuddyPosRequested()
    signal ringPreviewRequested()
    signal fxPreviewRequested(string kind)


    readonly property real headerH: 64
    readonly property real footerH: 50

    // ---- pages ----------------------------------------------------------------------
    // The page shown, by id. `tab` is the older 0/1/2 (General, Apps, Buddy), kept
    // for callers that open Settings on one of those.
    property string page: "general"
    property int tab: 0
    onTabChanged: page = ["general", "buddy"][Math.max(0, Math.min(1, tab))]

    readonly property var pageList: [
        { id: "general", group: qsTr("Notch"), label: qsTr("General"), icon: "settings",
          keys: "size scale notch auto hide delay fullscreen hotkey palette startup launch position" },
        { id: "buddy", group: qsTr("Buddy"), label: qsTr("Buddy"), icon: "spark",
          keys: "desktop buddy orb companion sound effects chime volume position chat" },
        { id: "look", group: qsTr("Buddy"), label: qsTr("Look & glow"), icon: "droplet",
          keys: "ring colour color style neon aurora comet orbit beads pulse prism halo thickness speed glow shadow bloom floor listening speaking animation waves screen cursor" },
        { id: "voice", group: qsTr("Buddy"), label: qsTr("Voice"), icon: "mic",
          keys: "microphone speech text wake word hey buddy moonshine transcription read aloud speak tts google cloud kokoro voice" },
        { id: "deepgram", group: qsTr("Buddy"), label: qsTr("Deepgram"), icon: "activity",
          keys: "deepgram live conversation voice agent hands-free talk speech listen hear think speak stt tts aura nova flux haiku claude model prompt api key interrupt barge headphones speakers" },
        { id: "model", group: qsTr("Agent"), label: qsTr("AI model"), icon: "cpu",
          keys: "provider openai anthropic ollama custom api key model endpoint temperature test connection" },
        { id: "agent", group: qsTr("Agent"), label: qsTr("Agent & tools"), icon: "terminal",
          keys: "tools bash shell computer use editor code search memory web fetch tasks steps screenshots detail stream thinking jev classifier classify intent request mem0 memory remember docker long-term" },
        { id: "connectors", group: qsTr("Agent"), label: qsTr("Connectors"), icon: "layers",
          keys: "connectors apps accounts gmail google email mail inbox draft reply calendar drive slack github notion mcp server oauth sign in connect disconnect integrations" },
        { id: "safety", group: qsTr("Agent"), label: qsTr("Safety"), icon: "shield-check",
          keys: "permission mode approve approval auto-approve bypass trusted plan ask rules always allow workspace redact secrets traces instructions commands" }
    ]
    property string search: ""
    readonly property var shownPages: {
        const q = search.trim().toLowerCase()
        if (q.length === 0)
            return pageList
        const out = []
        for (let i = 0; i < pageList.length; ++i) {
            const p = pageList[i]
            if ((p.label + " " + p.group + " " + p.keys).toLowerCase().indexOf(q) >= 0)
                out.push(p)
        }
        return out
    }
    function currentIndex() {
        for (let i = 0; i < pageList.length; ++i)
            if (pageList[i].id === page)
                return i
        return 0
    }
    function currentLabel() { return pageList[currentIndex()].label }
    function step(delta) {
        const n = pageList.length
        page = pageList[(currentIndex() + delta + n) % n].id
    }

    focus: true
    Keys.onPressed: (event) => {
        if (event.modifiers & Qt.ControlModifier) {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                step(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                event.accepted = true
            } else if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9 && event.key - Qt.Key_1 < pageList.length) {
                page = pageList[event.key - Qt.Key_1].id
                event.accepted = true
            } else if (event.key === Qt.Key_F) {
                searchField.forceActiveFocus()
                event.accepted = true
            }
        }
    }

    // ---- approvals: one switch for "approve everything" ------------------------------------
    property string modeBeforeAutoApprove: "default"
    function modeLabel(m) {
        return ({ acceptEdits: qsTr("auto-editing"), plan: qsTr("plan mode") })[m] || qsTr("asking first")
    }
    function setAutoApproveAll(on) {
        if (on) {
            if (prefs.buddyPermissionMode !== "bypass")
                modeBeforeAutoApprove = prefs.buddyPermissionMode
            prefs.buddyPermissionMode = "bypass"
        } else {
            prefs.buddyPermissionMode = modeBeforeAutoApprove === "bypass" ? "default" : modeBeforeAutoApprove
        }
    }

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
        opencode: {
            label: "OpenCode", base: "https://opencode.ai/zen", needsKey: true,
            keyHint: "Free from opencode.ai (email sign-up, no card). The *-free models cost nothing.",
            // OpenCode Zen's free models (verified against /zen/v1/models,
            // Oct 2026); "Test connection" refreshes this list from the server.
            models: ["glm-5.3-flash-free", "deepseek-v4.1-flash-free", "big-pickle",
                     "beam-free", "mimo-v2.6-flash-free", "nemotron-3.5-super-vl-free",
                     "nemotron-3.5-lightning-free", "minimax-m3.1-flash-free",
                     "jev-1.13-free", "longcat-2.5-preview-free"]
        },
        custom: { label: "Custom", base: "", needsKey: false, models: [] }
    })
    property string lastProvider: ""
    // Every configured provider's {key, base, model, models}: persists across
    // restarts (as JSON in prefs), so switching providers loses nothing.
    property var providerMemory: ({})
    function loadProviderMemory() {
        try {
            const parsed = JSON.parse(prefs.buddyProviderConfigs || "{}")
            providerMemory = (parsed && typeof parsed === "object") ? parsed : {}
        } catch (e) {
            providerMemory = {}
        }
    }
    function saveProviderMemory() {
        if (!prefs)
            return
        prefs.buddyProviderConfigs = JSON.stringify(providerMemory || {})
    }
    function rememberProvider(p) {
        // Stash the full config of the provider being left.
        providerMemory[p] = {
            key: String(prefs.buddyApiKey || ""),
            base: String(prefs.buddyApiBase || ""),
            model: String(prefs.buddyModel || "")
        }
        saveProviderMemory()
    }
    property var remoteModels: []
    property string testState: "idle"     // idle | testing | ok | failed
    property string testMessage: ""
    property string classifierTestState: "idle"     // idle | testing | ok | failed
    property string classifierTestMessage: ""
    property string memTestState: "idle"     // idle | testing | ok | failed
    property string memTestMessage: ""

    readonly property bool realChat: prefs && prefs.buddyProvider !== "none"
    readonly property bool buddyMovedCustom: prefs && (prefs.buddyX >= 0 || prefs.buddyY >= 0)

    function infoFor(p) { return settings.providerInfo[p] || settings.providerInfo.custom }
    function presetsFor(p) { return settings.infoFor(p).models }
    function labelFor(p) { return settings.infoFor(p).label }
    function showsKeyFor(p) { return p !== "ollama" }

    function chatEndpoint() {
        const p = settings.prefs ? settings.prefs.buddyProvider : "none"
        const base = settings.prefs ? settings.prefs.buddyApiBase : ""
        if (BuddyAgent)
            return BuddyAgent.chatUrl()
        const b = (String(base).trim() || settings.infoFor(p).base).replace(/\/+$/, "")
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
        const presets = settings.presetsFor(settings.prefs ? settings.prefs.buddyProvider : "none")
        for (let i = 0; i < presets.length; ++i)
            add(presets[i])
        // The models this provider served last time (persist per provider),
        // then the ones just fetched.
        const stored = settings.providerMemory[settings.prefs ? settings.prefs.buddyProvider : ""] || {}
        const storedModels = stored.models || []
        for (let i = 0; i < storedModels.length; ++i)
            add(storedModels[i])
        for (let i = 0; i < settings.remoteModels.length; ++i)
            add(settings.remoteModels[i])
        return out
    }

    function markDirty() {
        settings.testState = "idle"
        settings.testMessage = ""
        settings.remoteModels = []
    }

    function setModel(m) {
        settings.prefs.buddyModel = m
        settings.markDirty()
    }

    function pickProvider(v) {
        const from = settings.prefs.buddyProvider
        if (v === from || !v || v === "none")
            return
        settings.rememberProvider(from)
        settings.prefs.buddyProvider = v
        settings.lastProvider = v

        const was = settings.providerMemory[v] || {}
        const presets = settings.presetsFor(v)
        settings.prefs.buddyApiBase = (was.base !== undefined && was.base.length > 0)
            ? was.base : settings.infoFor(v).base
        settings.prefs.buddyApiKey = was.key !== undefined ? was.key : ""
        settings.prefs.buddyModel = (was.model && was.model.length > 0)
            ? was.model : (presets.length > 0 ? presets[0] : "")
        settings.markDirty()
        settings.remoteModels = (was.models && was.models.length > 0) ? was.models : []
    }

    function setRealChat(on) {
        if (!on) {
            if (settings.prefs.buddyProvider !== "none")
                settings.lastProvider = settings.prefs.buddyProvider
            settings.prefs.buddyProvider = "none"
            return
        }
        settings.pickProvider(settings.lastProvider !== "" ? settings.lastProvider : "openai")
    }

    function runTest() {
        settings.testState = "testing"
        settings.testMessage = qsTr("Checking %1…").arg(BuddyAgent.modelsUrl())
        BuddyAgent.probe()
    }

    function runClassifierTest() {
        settings.classifierTestState = "testing"
        settings.classifierTestMessage = qsTr("Asking %1…").arg(BuddyAgent.classifierUrl())
        BuddyAgent.testClassifier()
    }
    function classifierEdited() {
        settings.classifierTestState = "idle"
        settings.classifierTestMessage = ""
    }

    function runMemTest() {
        settings.memTestState = "testing"
        settings.memTestMessage = qsTr("Asking %1…").arg(BuddyAgent.memBaseUrl())
        BuddyAgent.testMem()
    }
    function memEdited() {
        settings.memTestState = "idle"
        settings.memTestMessage = ""
    }

    Connections {
        target: BuddyAgent
        function onMemTestFinished(ok, message) {
            settings.memTestState = ok ? "ok" : "failed"
            settings.memTestMessage = message
        }
        function onClassifierTestFinished(ok, message) {
            settings.classifierTestState = ok ? "ok" : "failed"
            settings.classifierTestMessage = message
        }
        function onTestFinished(ok, message) {
            settings.testState = ok ? "ok" : "failed"
            settings.testMessage = message
        }
        function onModelsFetched(models) {
            settings.remoteModels = models || []
            // Remember what this provider serves, so its chips survive a restart.
            const p = settings.prefs ? settings.prefs.buddyProvider : ""
            if (p && p !== "none" && models && models.length > 0) {
                const entry = settings.providerMemory[p] || {}
                entry.models = models.slice(0, 25)
                settings.providerMemory[p] = entry
                settings.saveProviderMemory()
            }
        }
    }


    // Wide by default, always clamped to the screen. On a narrow screen the sidebar
    // shrinks to icons.
    implicitWidth: Theme.fit(Theme.settingsWidth, maxPanelWidth)
    readonly property bool compact: width < 760
    readonly property real sidebarW: compact ? 62 : 216
    // One steady height for every page, so switching pages never makes the panel jump.
    readonly property real targetBodyHeight: Math.min(Math.max(380, maxBodyHeight), 600)
    implicitHeight: headerH + 1 + content.height + 1 + footerH
    readonly property real targetHeight: headerH + 1 + targetBodyHeight + 1 + footerH

    Component.onCompleted: {
        loadProviderMemory()
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
        height: settings.headerH

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 22
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
                text: settings.currentLabel()
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
            }
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
            onClicked: settings.closeRequested()
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
        height: settings.targetBodyHeight
        clip: true

        // ---- sidebar ---------------------------------------------------------------
        Rectangle {
            id: sidebar
            width: settings.sidebarW
            height: parent.height
            color: Theme.stone900

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.borderSoft
            }

            // search
            Rectangle {
                id: searchBox
                visible: !settings.compact
                x: 12
                y: 12
                width: sidebar.width - 24
                height: 34
                radius: 10
                color: Theme.field
                border.width: 1
                border.color: searchField.activeFocus ? Theme.accent : Theme.fieldBorder
                Behavior on border.color { ColorAnimation { duration: 130 } }

                Glyph {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 14
                    color: Theme.textFaint
                }
                TextInput {
                    id: searchField
                    x: 32
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 32 - (clearSearch.visible ? 26 : 10)
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.inverse
                    font.family: Theme.font
                    font.pixelSize: Theme.fontMd
                    clip: true
                    selectByMouse: true
                    text: settings.search
                    onTextEdited: settings.search = text
                    Keys.onReturnPressed: (event) => {
                        if (settings.shownPages.length > 0)
                            settings.page = settings.shownPages[0].id
                        event.accepted = true
                    }
                    Keys.onEscapePressed: (event) => {
                        if (text.length > 0) {
                            text = ""
                            settings.search = ""
                            event.accepted = true
                        }
                    }
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Search settings")
                    Text {
                        visible: searchField.text.length === 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Search settings")
                        color: Theme.textFaint
                        font: searchField.font
                    }
                }
                Item {
                    id: clearSearch
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    Glyph {
                        anchors.centerIn: parent
                        name: "close"
                        size: 10
                        stroke: 2.2
                        color: clearHit.containsMouse ? Theme.text : Theme.textFaint
                    }
                    MouseArea {
                        id: clearHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { searchField.text = ""; settings.search = "" }
                    }
                }
            }

            Flickable {
                id: navFlick
                anchors.top: settings.compact ? parent.top : searchBox.bottom
                anchors.topMargin: settings.compact ? 12 : 10
                anchors.bottom: parent.bottom
                width: parent.width
                contentWidth: width
                contentHeight: nav.implicitHeight + 16
                interactive: contentHeight > height + 1
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Column {
                    id: nav
                    x: 10
                    width: sidebar.width - 20
                    spacing: 2

                    Repeater {
                        model: settings.shownPages

                        Column {
                            id: navEntry
                            required property var modelData
                            required property int index
                            width: nav.width
                            spacing: 2
                            readonly property bool firstOfGroup: settings.search.length === 0
                                && (index === 0 || settings.shownPages[index - 1].group !== modelData.group)

                            Text {
                                visible: navEntry.firstOfGroup && !settings.compact
                                width: parent.width
                                topPadding: navEntry.index === 0 ? 2 : 14
                                bottomPadding: 3
                                leftPadding: 8
                                text: navEntry.modelData.group.toUpperCase()
                                color: Theme.textFaint
                                font.family: Theme.font
                                font.pixelSize: Theme.fontXs
                                font.weight: Font.Bold
                                font.letterSpacing: 0.9
                            }
                            Item {
                                visible: navEntry.firstOfGroup && settings.compact && navEntry.index > 0
                                width: parent.width
                                height: 8
                                Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: Theme.borderSoft }
                            }
                            SettingsSidebarItem {
                                width: parent.width
                                text: settings.compact ? "" : navEntry.modelData.label
                                icon: navEntry.modelData.icon
                                selected: settings.page === navEntry.modelData.id
                                onClicked: settings.page = navEntry.modelData.id
                            }
                        }
                    }

                    Text {
                        visible: settings.shownPages.length === 0
                        width: parent.width
                        padding: 10
                        text: qsTr("Nothing matches “%1”.").arg(settings.search)
                        color: Theme.textFaint
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ---- pages -------------------------------------------------------------------
        Item {
            id: pageArea
            x: sidebar.width
            width: parent.width - sidebar.width
            height: parent.height

            Component { id: generalC; GeneralPage { panel: settings } }
            Component { id: buddyC; BuddyPage { panel: settings } }
            Component { id: lookC; LookPage { panel: settings } }
            Component { id: voiceC; VoicePage { panel: settings } }
            Component { id: deepgramC; DeepgramPage { panel: settings } }
            Component { id: modelC; ModelPage { panel: settings } }
            Component { id: agentC; AgentPage { panel: settings } }
            Component { id: connectorsC; ConnectorsPage { panel: settings } }
            Component { id: safetyC; SafetyPage { panel: settings } }
            readonly property var components: ({
                general: generalC, buddy: buddyC, look: lookC,
                voice: voiceC, deepgram: deepgramC, model: modelC, agent: agentC, connectors: connectorsC, safety: safetyC
            })

            Repeater {
                model: settings.pageList

                Loader {
                    id: slot
                    required property var modelData
                    readonly property bool wanted: settings.page === modelData.id
                    anchors.fill: parent
                    active: false
                    sourceComponent: pageArea.components[modelData.id]
                    // Built when first visited, then kept, so coming back is instant.
                    onWantedChanged: if (wanted) active = true
                    Component.onCompleted: if (wanted) active = true
                    visible: wanted || opacity > 0.01
                    opacity: wanted ? 1 : 0
                    enabled: wanted
                    x: wanted ? 0 : 14
                    Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                    Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
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
        height: settings.footerH

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
                    text: "v" + settings.version
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
                onClicked: settings.quitRequested()
            }
        }
    }
}
