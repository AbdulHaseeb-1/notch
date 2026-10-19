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
