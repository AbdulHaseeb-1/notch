pragma ComponentBehavior: Bound
import QtQuick

// Connected apps: sign Buddy in to your accounts once, in the browser, and it can read
// your mail, draft replies and work with your GitHub. There is nothing to paste and no
// key to copy: each app below has one Connect button, the sign-in finishes in the
// browser, and every action that changes something is asked about first (see Safety).
SettingsPage {
    id: page
    required property var panel
    title: qsTr("Connectors")
    subtitle: qsTr("Let Buddy use your accounts in other apps: read your mail, draft replies, check your GitHub.")

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

    // Only for the remove confirmation on servers saved by older versions.
    property string confirmRemove: ""

    function stateColor(s) {
        return s === "connected" ? Theme.success : s === "error" ? Theme.danger : s === "connecting" ? Theme.warning : Theme.textFaint
    }
    function stateLabel(s) {
        return s === "connected" ? qsTr("Connected") : s === "error" ? qsTr("Problem") : s === "connecting" ? qsTr("Connecting…") : qsTr("Not connected")
    }

    // ---- master switch ---------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Connected apps")
        rightText: page.connectedCount > 0 ? qsTr("%1 connected").arg(page.connectedCount) : qsTr("None connected")
        description: qsTr("Connect an app once — one click, then approve in your browser — and just ask: “read my unread emails”, “what's open on my repos”.")
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
                            onClicked: page.registry.connectApp(card.modelData.id)
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

                // Servers saved by older versions can still be removed here.
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
