pragma ComponentBehavior: Bound
import QtQuick

// Connected apps: sign Buddy in to your accounts once, and it can read your mail, draft
// replies and use any app that has an MCP server. Every action that changes something is
// asked about first (see Safety); this page is where you connect, disconnect and see what
// each app lets Buddy do.
SettingsPage {
    id: page
    required property var panel
    title: qsTr("Connectors")
    subtitle: qsTr("Let Buddy use your accounts in other apps: read your mail, draft replies, check a calendar.")

    readonly property var registry: BuddyAgent.connectors
    readonly property var apps: registry ? registry.connectors : []
    readonly property int connectedCount: {
        let n = 0
        for (let i = 0; i < apps.length; ++i)
            if (apps[i].state === "connected")
                ++n
        return n
    }
    // Everything the user allowed for good, from the apps' tool lists.
    readonly property var grants: {
        const out = []
        for (let i = 0; i < apps.length; ++i)
            for (let j = 0; j < apps[i].tools.length; ++j)
                if (apps[i].tools[j].always)
                    out.push({ name: apps[i].tools[j].name, title: apps[i].name + ": " + apps[i].tools[j].title })
        return out
    }

    // What is typed in a form lives here, not in a row, so a status change does not wipe it.
    property string googleId: ""
    property string googleSecret: ""
    property bool googleLoaded: false
    property string mcpName: ""
    property string mcpTransport: "http"
    property string mcpTarget: ""
    property string mcpSecrets: ""
    property string mcpError: ""
    property string mcpHelp: ""
    property bool showAdd: false
    property string confirmRemove: ""

    function loadGoogle() {
        for (let i = 0; i < apps.length; ++i)
            if (apps[i].id === "gmail") {
                googleId = apps[i].clientId || ""
                googleLoaded = true
            }
    }
    onAppsChanged: if (!googleLoaded) loadGoogle()
    Component.onCompleted: loadGoogle()

    function stateColor(s) {
        return s === "connected" ? Theme.success : s === "error" ? Theme.danger : s === "connecting" ? Theme.warning : Theme.textFaint
    }
    function stateLabel(s) {
        return s === "connected" ? qsTr("Connected") : s === "error" ? qsTr("Problem") : s === "connecting" ? qsTr("Connecting…") : qsTr("Not connected")
    }
    function connectGoogle() {
        registry.setGoogleClientId(googleId)
        if (googleSecret.length > 0) {
            registry.setGoogleClientSecret(googleSecret)
            googleSecret = ""
        }
        registry.connectApp("gmail")
    }
    function addServer() {
        const err = registry.addMcpServer(mcpName, mcpTransport, mcpTarget, mcpSecrets)
        mcpError = err
        if (err.length === 0) {
            mcpName = ""; mcpTarget = ""; mcpSecrets = ""; mcpHelp = ""
            showAdd = false
        }
    }
    function usePreset(p) {
        mcpName = p.name
        mcpTransport = p.transport
        mcpTarget = p.target
        mcpSecrets = ""
        mcpHelp = p.help + "\n\n" + qsTr("Secret line to enter below: %1").arg(p.secretsHint)
        mcpError = ""
        showAdd = true
    }

    // ---- master switch ---------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Connected apps")
        rightText: page.connectedCount > 0 ? qsTr("%1 connected").arg(page.connectedCount) : qsTr("None connected")
        description: qsTr("Connect an app once and just ask: “read my unread emails”, “draft a reply to Anna”.")
    }

    SettingsNotice {
        visible: !panel.prefs.buddyAgentMode
        icon: "shield"
        text: qsTr("Agent tools are off, so Buddy cannot use connected apps. Turn them on under Agent & tools.")
        action: qsTr("Open Agent & tools")
        onActivated: panel.page = "agent"
    }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Let Buddy use connected apps")
            sub: qsTr("Off: nothing below is offered to Buddy, but your sign-ins are kept. Reading is automatic; anything that sends, creates or changes something asks you first, every time.")
            checked: panel.prefs.buddyAllowConnectors
            onToggled: (c) => panel.prefs.buddyAllowConnectors = c
        }
        Text {
            visible: page.registry && !page.registry.secretsEncrypted
            width: parent.width
            text: qsTr("This system cannot encrypt secrets at rest, so sign-ins are kept in memory only and you will have to connect again after a restart.")
            color: Theme.warning
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
    }

    // ---- one card per app -------------------------------------------------------------------
    Column {
        width: parent.width
        spacing: 10

        Repeater {
            model: page.apps

            SettingsGroupCard {
                id: card
                required property var modelData
                readonly property bool isGmail: modelData.id === "gmail"
                readonly property bool isMcp: modelData.kind === "mcp"
                readonly property string appState: modelData.state

                Row {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34
                        height: 34
                        radius: 10
                        color: Theme.tile
                        border.width: 1
                        border.color: Theme.border
                        Glyph {
                            anchors.centerIn: parent
                            name: card.modelData.icon
                            size: 18
                            color: Theme.ink
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 34 - 10 - btnRow.width - 10
                        spacing: 2
                        Row {
                            spacing: 8
                            Text {
                                text: card.modelData.name
                                color: Theme.ink
                                font.family: Theme.font
                                font.pixelSize: Theme.fontMd
                                font.weight: Font.Bold
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: page.stateColor(card.appState)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.stateLabel(card.appState)
                                color: page.stateColor(card.appState)
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            width: parent.width
                            text: card.modelData.status.length > 0 && card.appState !== "disconnected" ? card.modelData.status : card.modelData.summary
                            color: card.appState === "error" ? Theme.danger : Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                        }
                    }
                    Row {
                        id: btnRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        SettingsPillButton {
                            visible: card.appState === "disconnected" || card.appState === "error"
                            label: qsTr("Connect")
                            icon: "plus"
                            primary: true
                            enabled: !card.isGmail || page.googleId.trim().length > 0
                            onClicked: card.isGmail ? page.connectGoogle() : page.registry.connectApp(card.modelData.id)
                        }
                        SettingsPillButton {
                            visible: card.appState === "connecting"
                            label: qsTr("Cancel")
                            onClicked: page.registry.disconnectApp(card.modelData.id)
                        }
                        SettingsPillButton {
                            visible: card.appState === "connected"
                            label: qsTr("Disconnect")
                            danger: true
                            onClicked: page.registry.disconnectApp(card.modelData.id)
                        }
                    }
                }

                // What this app lets Buddy do.
                Column {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: card.modelData.permissions
                        Row {
                            id: permRow
                            required property var modelData
                            width: parent.width
                            spacing: 8
                            Glyph {
                                y: 2
                                name: permRow.modelData.write ? "shield-alert" : "eye"
                                size: 13
                                color: permRow.modelData.write ? Theme.warning : Theme.success
                            }
                            Text {
                                width: parent.width - 21
                                text: permRow.modelData.label
                                color: Theme.inkSoft
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                // Gmail needs the user's own Google OAuth client (Noch ships none).
                Column {
                    visible: card.isGmail && card.appState !== "connected"
                    width: parent.width
                    spacing: 8
                    SettingsCardDivider {}
                    Text {
                        width: parent.width
                        text: qsTr("Noch signs in with your own Google Cloud OAuth client, so no one else sits between you and Google. Create a “Desktop app” client once (steps in docs/connectors.md), switch on the Gmail API, and paste its ID here. Nothing is sent anywhere until you press Connect and approve in your browser.")
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        wrapMode: Text.WordWrap
                    }
                    SettingsFieldBox {
                        width: parent.width
                        label: qsTr("Google OAuth client ID")
                        placeholder: "1234567890-abc.apps.googleusercontent.com"
                        mono: true
                        text: page.googleId
                        onEdited: (v) => page.googleId = v
                        onAccepted: if (page.googleId.trim().length > 0) page.connectGoogle()
                    }
                    SettingsFieldBox {
                        width: parent.width
                        label: qsTr("Client secret (only if Google gave you one)")
                        placeholder: card.modelData.hasClientSecret ? qsTr("Saved, encrypted. Type to replace.") : qsTr("Optional")
                        hint: qsTr("Desktop clients get one, but it is not confidential. It is kept encrypted, never in plain settings.")
                        secret: true
                        mono: true
                        text: page.googleSecret
                        onEdited: (v) => page.googleSecret = v
                    }
                }

                // The tools Buddy gets, with their rules.
                Column {
                    visible: card.appState === "connected" && card.modelData.tools.length > 0
                    width: parent.width
                    spacing: 4
                    SettingsCardDivider {}
                    Text {
                        text: qsTr("Tools Buddy can call")
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: card.modelData.tools
                        Item {
                            id: toolRow
                            required property var modelData
                            width: parent.width
                            height: 24
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - ruleLabel.width - 12
                                text: toolRow.modelData.title
                                color: Theme.inkSoft
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                elide: Text.ElideRight
                            }
                            Text {
                                id: ruleLabel
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: !toolRow.modelData.write ? qsTr("runs without asking")
                                    : toolRow.modelData.always ? qsTr("always allowed") : qsTr("asks every time")
                                color: !toolRow.modelData.write ? Theme.success : toolRow.modelData.always ? Theme.warning : Theme.textFaint
                                font.family: Theme.font
                                font.pixelSize: Theme.fontXs
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                // MCP servers can be edited and removed.
                Row {
                    visible: card.isMcp
                    width: parent.width
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - removeBtn.width - 8
                        text: card.modelData.transport === "http" ? card.modelData.target : "> " + card.modelData.target
                        color: Theme.textFaint
                        font.family: "Consolas"
                        font.pixelSize: Theme.fontXs
                        elide: Text.ElideMiddle
                    }
                    SettingsPillButton {
                        id: removeBtn
                        anchors.verticalCenter: parent.verticalCenter
                        label: page.confirmRemove === card.modelData.id ? qsTr("Really remove?") : qsTr("Remove")
                        danger: true
                        icon: "trash"
                        onClicked: {
                            if (page.confirmRemove === card.modelData.id) {
                                page.confirmRemove = ""
                                page.registry.removeConnector(card.modelData.id)
                            } else {
                                page.confirmRemove = card.modelData.id
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- what is planned -----------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("More apps")
        description: qsTr("Google Calendar, Drive, Slack, Notion and GitHub are next. Any app with an MCP server works today: add it below.")
    }

    // ---- add an MCP server ------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Add an MCP server")
        rightText: page.showAdd ? "" : qsTr("Any app with an MCP server")
    }

    SettingsGroupCard {
        Row {
            visible: !page.showAdd
            width: parent.width
            spacing: 8
            SettingsPillButton {
                label: qsTr("Add a server")
                icon: "plus"
                primary: true
                onClicked: { page.showAdd = true; page.mcpError = "" }
            }
            Repeater {
                model: page.registry ? page.registry.presets() : []
                SettingsPillButton {
                    id: presetBtn
                    required property var modelData
                    label: presetBtn.modelData.name
                    onClicked: page.usePreset(presetBtn.modelData)
                }
            }
        }

        Column {
            visible: page.showAdd
            width: parent.width
            spacing: 10

            Text {
                width: parent.width
                text: qsTr("MCP (Model Context Protocol) is the standard way apps offer tools to assistants. Add a server and its tools join Buddy's: tools the server marks read-only run freely, everything else asks you every time. Only add servers you trust: a command runs on this computer with your rights, and what a server returns is treated as untrusted text.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Text {
                visible: page.mcpHelp.length > 0
                width: parent.width
                text: page.mcpHelp
                color: Theme.inkSoft
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            SettingsFieldBox {
                width: parent.width
                label: qsTr("Name")
                placeholder: "GitHub"
                text: page.mcpName
                onEdited: (v) => page.mcpName = v
            }
            Segmented {
                width: parent.width
                height: 34
                options: [
                    { label: qsTr("Web address (https)"), value: "http" },
                    { label: qsTr("Command on this PC"), value: "stdio" }
                ]
                value: page.mcpTransport
                onPicked: (v) => page.mcpTransport = v
            }
            SettingsFieldBox {
                width: parent.width
                label: page.mcpTransport === "http" ? qsTr("Server address") : qsTr("Command")
                placeholder: page.mcpTransport === "http" ? "https://example.com/mcp" : "npx -y some-mcp-server"
                mono: true
                text: page.mcpTarget
                onEdited: (v) => page.mcpTarget = v
            }
            SettingsTextArea {
                width: parent.width
                label: page.mcpTransport === "http" ? qsTr("Headers (optional, kept encrypted)") : qsTr("Environment variables (optional, kept encrypted)")
                placeholder: page.mcpTransport === "http" ? "Authorization: Bearer <token>" : "API_TOKEN=<token>"
                hint: page.mcpTransport === "http" ? qsTr("One “Name: value” per line. Sent only over https or to this computer.")
                                                   : qsTr("One NAME=value per line, added to the server's environment.")
                boxHeight: 70
                mono: true
                text: page.mcpSecrets
                onEdited: (v) => page.mcpSecrets = v
            }
            Text {
                visible: page.mcpError.length > 0
                width: parent.width
                text: page.mcpError
                color: Theme.danger
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 8
                SettingsPillButton {
                    label: qsTr("Add and connect")
                    icon: "plus"
                    primary: true
                    onClicked: page.addServer()
                }
                SettingsPillButton {
                    label: qsTr("Cancel")
                    onClicked: { page.showAdd = false; page.mcpError = ""; page.mcpHelp = "" }
                }
            }
        }
    }

    // ---- standing permissions ----------------------------------------------------------------------
    SettingsSectionHeader {
        visible: page.grants.length > 0
        title: qsTr("Always allowed")
        rightText: qsTr("%1 action(s)").arg(page.grants.length)
        description: qsTr("You chose “always allow” for these. Buddy does them without asking; a possible prompt injection in the same task still brings the question back.")
    }

    SettingsGroupCard {
        visible: page.grants.length > 0

        Repeater {
            model: page.grants
            Item {
                id: grantRow
                required property var modelData
                width: parent.width
                height: 30
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - revokeBtn.width - 10
                    text: grantRow.modelData.title
                    color: Theme.inkSoft
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    elide: Text.ElideRight
                }
                SettingsPillButton {
                    id: revokeBtn
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    label: qsTr("Ask again")
                    onClicked: page.registry.revokeAlwaysAllow(grantRow.modelData.name)
                }
            }
        }
        SettingsPillButton {
            visible: page.grants.length > 1
            label: qsTr("Ask again for all")
            danger: true
            onClicked: page.registry.revokeAllAlwaysAllow()
        }
    }
}
