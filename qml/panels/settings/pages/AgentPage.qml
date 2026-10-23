pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Agent & tools")
    subtitle: qsTr("What Buddy is allowed to do by itself: run commands, edit files, search, use the computer.")

    SettingsSectionHeader {
        title: qsTr("Anthropic Agent SDK & Tools")
        rightText: panel.prefs.buddyAgentMode
                     ? qsTr("%1 of 7 tool suites armed")
                         .arg((panel.prefs.buddyAllowBash ? 1 : 0)
                              + (panel.prefs.buddyAllowComputer ? 1 : 0)
                              + (panel.prefs.buddyAllowEditor ? 1 : 0)
                              + (panel.prefs.buddyAllowCodeSearch ? 1 : 0)
                              + (panel.prefs.buddyAllowMemory ? 1 : 0)
                              + (panel.prefs.buddyAllowWebSearch ? 1 : 0)
                              + (panel.prefs.buddyAllowTodoTask ? 1 : 0))
                     : qsTr("Disabled")
    }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Enable Agent SDK tool loop")
            sub: qsTr("Allow Anthropic and all configured AI agents to autonomously run multi-turn tool calls, execute commands, and use the computer.")
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

        SettingsToggleRow {
            visible: panel.prefs.buddyAgentMode
            label: qsTr("Control the music pill")
            sub: qsTr("Buddy can play, pause, skip and change the volume of the music pill when you ask, and queue songs from your Music folder. Files outside your Music folder are asked about first.")
            checked: panel.prefs.buddyAllowMusic
            onToggled: (c) => panel.prefs.buddyAllowMusic = c
        }

        Column {
            visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowMusic
            width: parent.width
            spacing: 8

            Text {
                width: parent.width
                text: qsTr("Songs from YouTube")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: Youtube.available
                      ? qsTr("Ready%1. Ask Buddy to play any song or artist and it plays in the music pill: no browser opens. It uses yt-dlp, which fetches the audio from YouTube.")
                            .arg(Youtube.version.length > 0 ? " (yt-dlp " + Youtube.version + ")" : "")
                      : qsTr("Buddy can play any song from YouTube in the music pill with no browser, using the free yt-dlp program. It is not installed yet.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Flow {
                width: parent.width
                spacing: 8
                SettingsPillButton {
                    primary: !Youtube.available
                    icon: "down"
                    label: Youtube.state === "installing" ? qsTr("Installing…") : (Youtube.available ? qsTr("Reinstall") : qsTr("Install yt-dlp"))
                    enabled: Youtube.state === "idle"
                    onClicked: Youtube.install()
                }
                SettingsPillButton {
                    visible: Youtube.available
                    icon: "reset"
                    label: Youtube.state === "updating" ? qsTr("Updating…") : qsTr("Update")
                    enabled: Youtube.state === "idle"
                    onClicked: Youtube.update()
                }
                SettingsPillButton {
                    icon: "folder"
                    label: qsTr("Open folder")
                    onClicked: Youtube.openToolsFolder()
                }
            }
            Text {
                visible: Youtube.message.length > 0
                width: parent.width
                text: Youtube.message
                color: Youtube.messageIsError ? Theme.warning : Theme.success
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: qsTr("Install downloads the official yt-dlp release from github.com/yt-dlp and checks it against the checksum published with it before using it. Nothing is downloaded unless you press the button. yt-dlp needs to be kept up to date: YouTube changes often.")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            SettingsFieldBox {
                width: parent.width
                label: qsTr("Use a copy of yt-dlp from somewhere else")
                placeholder: qsTr("Leave empty to use the installed one")
                mono: true
                text: panel.prefs.buddyYtDlpPath
                onEdited: (v) => panel.prefs.buddyYtDlpPath = v.trim()
            }
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
    }

    // ---- Agent Skills ----------------------------------------------------------------
    Item {
        id: skillsState
        visible: false
        height: 0
        property var rows: []
        property var notes: []
        readonly property int onCount: {
            let n = 0
            for (let i = 0; i < rows.length; ++i)
                if (rows[i].enabled)
                    ++n
            return n
        }
        function refresh() {
            rows = BuddyAgent.skillList()
            notes = BuddyAgent.skillNotes()
        }
        function setEnabled(name, on) {
            const cur = String(panel.prefs.buddyDisabledSkills).split(",").map(s => s.trim()).filter(s => s.length > 0)
            const at = cur.indexOf(name)
            if (on && at >= 0)
                cur.splice(at, 1)
            else if (!on && at < 0)
                cur.push(name)
            panel.prefs.buddyDisabledSkills = cur.join(",")
        }
        Component.onCompleted: refresh()
        Connections {
            target: BuddyAgent
            function onSkillsChanged() { skillsState.refresh() }
        }
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyAgentMode
        title: qsTr("Skills")
        rightText: !panel.prefs.buddyAllowSkills ? qsTr("Off")
                 : qsTr("%1 of %2 on").arg(skillsState.onCount).arg(skillsState.rows.length)
    }

    SettingsGroupCard {
        visible: panel.prefs.buddyAgentMode

        SettingsToggleRow {
            label: qsTr("Use skills")
            sub: qsTr("A skill is a folder with a SKILL.md that teaches Buddy a better way to do one kind of task. Only each skill's name and description are shown to Buddy; it reads the rest when a task matches. A skill is advice only: it never gives Buddy a tool or permission, and everything it leads to still asks first as usual.")
            checked: panel.prefs.buddyAllowSkills
            onToggled: (c) => panel.prefs.buddyAllowSkills = c
        }

        SettingsCardDivider { visible: panel.prefs.buddyAllowSkills && skillsState.rows.length > 0 }

        Repeater {
            model: panel.prefs.buddyAllowSkills ? skillsState.rows : []
            delegate: Column {
                id: skillRow
                required property var modelData
                required property int index
                width: parent ? parent.width : 300
                spacing: 8
                SettingsToggleRow {
                    label: "/" + skillRow.modelData.name + " · " + skillRow.modelData.sourceLabel
                    sub: skillRow.modelData.description
                         + (skillRow.modelData.overrides.length > 0 ? " " + qsTr("(replaces a %1 skill of the same name)").arg(skillRow.modelData.overrides) : "")
                         + (!skillRow.modelData.modelInvocable ? " " + qsTr("(only you can run it)") : "")
                         + (!skillRow.modelData.userInvocable ? " " + qsTr("(only Buddy can run it)") : "")
                    checked: skillRow.modelData.enabled
                    onToggled: (c) => skillsState.setEnabled(skillRow.modelData.name, c)
                }
                SettingsCardDivider { visible: skillRow.index < skillsState.rows.length - 1 }
            }
        }

        Text {
            visible: panel.prefs.buddyAllowSkills && skillsState.rows.length === 0
            width: parent.width
            text: qsTr("No skills found yet.")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
        }

        Text {
            visible: panel.prefs.buddyAllowSkills && skillsState.notes.length > 0
            width: parent.width
            text: qsTr("Skipped: %1").arg(skillsState.notes.join("; "))
            color: Theme.danger
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }

        SettingsCardDivider { visible: panel.prefs.buddyAllowSkills }

        Row {
            visible: panel.prefs.buddyAllowSkills
            width: parent.width
            spacing: 12

            SettingsPillButton {
                id: skillsFolderBtn
                anchors.verticalCenter: parent.verticalCenter
                icon: "folder"
                label: qsTr("Open skills folder")
                primary: true
                onClicked: Qt.openUrlExternally("file:///" + BuddyAgent.ensureSkillsFolder().replace(/\\/g, "/"))
            }

            SettingsPillButton {
                id: skillsRescanBtn
                anchors.verticalCenter: parent.verticalCenter
                icon: "reset"
                label: qsTr("Rescan")
                onClicked: skillsState.refresh()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - skillsFolderBtn.width - skillsRescanBtn.width - 24)
                text: qsTr("Add a folder with a SKILL.md here, or in .noch/skills of a workspace. Type /skills in the chat to see them, or /name to run one.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowMemory
        title: qsTr("Long-term memory (Mem0)")
        rightText: !panel.prefs.buddyMemEnabled ? qsTr("Off")
                 : panel.prefs.buddyMemBackend === "platform" ? qsTr("Mem0 Cloud")
                 : panel.prefs.buddyMemBackend === "custom" ? qsTr("Custom server")
                 : qsTr("Local · Docker")
    }

    SettingsGroupCard {
        visible: panel.prefs.buddyAgentMode && panel.prefs.buddyAllowMemory

        SettingsToggleRow {
            label: qsTr("Remember across conversations")
            sub: qsTr("Before Buddy answers, what it knows about you is looked up and added to its prompt. After each reply, the exchange is sent to Mem0, which keeps only lasting facts (preferences, projects, setup). If Mem0 is unreachable Buddy carries on with its local notes.")
            checked: panel.prefs.buddyMemEnabled
            onToggled: (c) => { panel.prefs.buddyMemEnabled = c; panel.memEdited() }
        }

        SettingsCardDivider { visible: panel.prefs.buddyMemEnabled }

        Column {
            visible: panel.prefs.buddyMemEnabled
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: qsTr("Where memories are kept")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
            }
            Segmented {
                width: parent.width
                height: 34
                options: [
                    { label: qsTr("Local"), value: "local" },
                    { label: qsTr("Mem0 Cloud"), value: "platform" },
                    { label: qsTr("Custom"), value: "custom" }
                ]
                value: panel.prefs.buddyMemBackend
                onPicked: (v) => { panel.prefs.buddyMemBackend = v; panel.memEdited() }
            }
            Text {
                width: parent.width
                text: panel.prefs.buddyMemBackend === "platform"
                      ? qsTr("Your conversations are sent to Mem0's servers (api.mem0.ai) under your Mem0 account. Get a key at app.mem0.ai.")
                      : panel.prefs.buddyMemBackend === "custom"
                        ? qsTr("Any server that speaks the Mem0 Platform API (/v3/memories/add/ and /search/), for example your own Mem0 deployment.")
                        : qsTr("A Mem0 server in Docker on this computer (docker/mem0). Your memories stay on this PC. It uses the chat provider you set up under AI model for its two models, so there is no second key to enter. Run docker/mem0/setup.ps1 once to start it.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
        }

        Flow {
            visible: panel.prefs.buddyMemEnabled && panel.prefs.buddyMemBackend !== "local"
            width: parent.width
            spacing: 12

            readonly property bool pair: width >= 520
            readonly property real halfW: pair ? Math.floor((width - spacing) / 2) : width

            SettingsFieldBox {
                visible: panel.prefs.buddyMemBackend === "custom"
                width: parent.halfW
                label: qsTr("Server address")
                placeholder: "https://mem0.example.com"
                hint: qsTr("Sent over https, or to this computer. The API key is never sent in clear text across a network.")
                mono: true
                text: panel.prefs.buddyMemUrl
                onEdited: (v) => { panel.prefs.buddyMemUrl = v; panel.memEdited() }
            }

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("API key")
                placeholder: panel.prefs.buddyMemBackend === "platform" ? "m0-..." : qsTr("Server key")
                secret: true
                mono: true
                text: panel.prefs.buddyMemKey
                onEdited: (v) => { panel.prefs.buddyMemKey = v; panel.memEdited() }
            }
        }

        Flow {
            visible: panel.prefs.buddyMemEnabled && panel.prefs.buddyMemBackend === "local"
            width: parent.width
            spacing: 12

            readonly property bool pair: width >= 520
            readonly property real halfW: pair ? Math.floor((width - spacing) / 2) : width

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("Extraction model")
                placeholder: panel.prefs.buddyModel.length > 0 ? panel.prefs.buddyModel : qsTr("Same as the chat model")
                hint: qsTr("Reads each exchange and keeps the lasting facts. A small, cheap model is enough; leave blank to use the chat model.")
                mono: true
                text: panel.prefs.buddyMemLlmModel
                onEdited: (v) => { panel.prefs.buddyMemLlmModel = v.trim(); panel.memEdited() }
            }

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("Embedding model")
                placeholder: panel.prefs.buddyProvider === "ollama" ? "nomic-embed-text" : "text-embedding-3-small"
                hint: qsTr("Used to search by meaning. It must be an embedding model your provider serves. Changing it starts a separate, empty memory.")
                mono: true
                text: panel.prefs.buddyMemEmbedModel
                onEdited: (v) => { panel.prefs.buddyMemEmbedModel = v.trim(); panel.memEdited() }
            }

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("Embedding address (optional)")
                placeholder: qsTr("Same as the chat provider")
                hint: qsTr("Only if embeddings come from somewhere else, for example https://api.openai.com/v1.")
                mono: true
                text: panel.prefs.buddyMemEmbedBase
                onEdited: (v) => { panel.prefs.buddyMemEmbedBase = v.trim(); panel.memEdited() }
            }

            SettingsFieldBox {
                visible: panel.prefs.buddyMemEmbedBase.length > 0
                width: parent.halfW
                label: qsTr("Embedding API key")
                placeholder: qsTr("Same as the chat provider")
                secret: true
                mono: true
                text: panel.prefs.buddyMemEmbedKey
                onEdited: (v) => { panel.prefs.buddyMemEmbedKey = v; panel.memEdited() }
            }
        }

        SettingsFieldBox {
            visible: panel.prefs.buddyMemEnabled && panel.prefs.buddyMemBackend === "local"
            width: parent.width
            label: qsTr("Server key (optional)")
            placeholder: BuddyAgent.memHasKey() ? qsTr("Found: set up by the local stack") : qsTr("Not found: run docker/mem0/setup.ps1")
            hint: qsTr("Leave blank to use the key setup.ps1 saved in %1.").arg(BuddyAgent.memLocalKeyPath())
            secret: true
            mono: true
            text: panel.prefs.buddyMemKey
            onEdited: (v) => { panel.prefs.buddyMemKey = v; panel.memEdited() }
        }

        SettingsFieldBox {
            visible: panel.prefs.buddyMemEnabled
            width: parent.width
            label: qsTr("Memory name")
            placeholder: "noch"
            hint: qsTr("Memories are filed under this name. Change it to keep separate sets of memories.")
            mono: true
            text: panel.prefs.buddyMemUserId
            onEdited: (v) => { panel.prefs.buddyMemUserId = v.trim(); panel.memEdited() }
        }

        SettingsCardDivider { visible: panel.prefs.buddyMemEnabled }

        Row {
            visible: panel.prefs.buddyMemEnabled
            width: parent.width
            spacing: 12

            SettingsPillButton {
                id: memTestBtn
                anchors.verticalCenter: parent.verticalCenter
                icon: "wifi"
                label: panel.memTestState === "testing" ? qsTr("Testing…") : qsTr("Test memory")
                primary: true
                enabled: panel.memTestState !== "testing"
                onClicked: panel.runMemTest()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - memTestBtn.width - 12)
                text: panel.memTestMessage.length > 0
                      ? panel.memTestMessage
                      : qsTr("Searches the memory with a sample query to check the address and key.")
                color: panel.memTestState === "ok" ? Theme.success
                     : panel.memTestState === "failed" ? Theme.danger : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyAgentMode
        title: qsTr("Request classifier (JEV)")
        rightText: panel.prefs.buddyClassifierEnabled ? panel.prefs.buddyClassifierModel : qsTr("Off")
    }

    SettingsGroupCard {
        visible: panel.prefs.buddyAgentMode

        SettingsToggleRow {
            label: qsTr("Classify each request first")
            sub: qsTr("A small model labels what you asked (question, research, code, desktop) before Buddy starts, so it can ask before changing anything on a plain question. Your request text is sent to the address below, not to your chat provider. If it is slow or fails, Buddy just carries on without it.")
            checked: panel.prefs.buddyClassifierEnabled
            onToggled: (c) => { panel.prefs.buddyClassifierEnabled = c; panel.classifierEdited() }
        }

        SettingsCardDivider { visible: panel.prefs.buddyClassifierEnabled }

        Flow {
            visible: panel.prefs.buddyClassifierEnabled
            width: parent.width
            spacing: 12

            readonly property bool pair: width >= 520
            readonly property real halfW: pair ? Math.floor((width - spacing) / 2) : width

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("Base URL")
                placeholder: "https://openrouter.ai/api/v1/systemone"
                hint: qsTr("POST %1. An address ending in /systemone or /decisions is a decision model; any other is a chat endpoint.").arg(BuddyAgent.classifierUrl())
                mono: true
                text: panel.prefs.buddyClassifierBase
                onEdited: (v) => { panel.prefs.buddyClassifierBase = v; panel.classifierEdited() }
            }

            SettingsFieldBox {
                width: parent.halfW
                label: qsTr("API key")
                placeholder: qsTr("Uses your chat key")
                hint: qsTr("Leave blank to reuse your chat provider's key when it is the same https host. Sent over https only.")
                secret: true
                mono: true
                text: panel.prefs.buddyClassifierKey
                onEdited: (v) => { panel.prefs.buddyClassifierKey = v; panel.classifierEdited() }
            }

            SettingsFieldBox {
                width: parent.width
                label: qsTr("Model")
                placeholder: "inception/mercury-decide:free"
                hint: qsTr("A decision model, e.g. inception/mercury-decide:free, respan/span-01-lite:free or jev-latest. Use Test JEV to check it answers.")
                mono: true
                text: panel.prefs.buddyClassifierModel
                onEdited: (v) => { panel.prefs.buddyClassifierModel = v; panel.classifierEdited() }
            }
        }

        SettingsCardDivider { visible: panel.prefs.buddyClassifierEnabled }

        Row {
            visible: panel.prefs.buddyClassifierEnabled
            width: parent.width
            spacing: 12

            SettingsPillButton {
                id: classifierTestBtn
                anchors.verticalCenter: parent.verticalCenter
                icon: "wifi"
                label: panel.classifierTestState === "testing" ? qsTr("Testing…") : qsTr("Test JEV")
                primary: true
                enabled: panel.classifierTestState !== "testing"
                onClicked: panel.runClassifierTest()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - classifierTestBtn.width - 12)
                text: panel.classifierTestMessage.length > 0
                      ? panel.classifierTestMessage
                      : qsTr("Sends a sample request and checks that the reply is a valid label.")
                color: panel.classifierTestState === "ok" ? Theme.success
                     : panel.classifierTestState === "failed" ? Theme.danger : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }
}
