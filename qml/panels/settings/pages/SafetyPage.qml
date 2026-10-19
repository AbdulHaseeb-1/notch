pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Safety & permissions")
    subtitle: qsTr("When Buddy asks first, what is always blocked, and what it remembers.")
    property bool confirmingAutoApprove: false
    SettingsNotice {
        visible: !panel.prefs.buddyAgentMode
        icon: "shield"
        text: qsTr("Agent tools are off, so there is nothing to approve. Turn them on under Agent & tools.")
        action: qsTr("Open Agent & tools")
        onActivated: panel.page = "agent"
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyAgentMode
        title: qsTr("Approvals")
        rightText: panel.prefs.buddyPermissionMode === "bypass" ? qsTr("Everything auto-approved") : qsTr("You approve")
        description: qsTr("Decide how much Buddy may do before asking you.")
    }
    SettingsGroupCard {
        visible: panel.prefs.buddyAgentMode

        SettingsToggleRow {
            label: qsTr("Auto-approve all permission requests")
            sub: qsTr("Every tool call from every chat runs without asking: commands, file edits, computer control. The hard stops below still apply.")
            checked: panel.prefs.buddyPermissionMode === "bypass" || page.confirmingAutoApprove
            onToggled: (c) => {
                if (c) {
                    page.confirmingAutoApprove = true
                } else {
                    page.confirmingAutoApprove = false
                    panel.setAutoApproveAll(false)
                }
            }
        }

        SettingsNotice {
            visible: page.confirmingAutoApprove
            warning: true
            icon: "shield-alert"
            text: qsTr("Buddy will be able to change files, run programs and click for you with no questions. Only turn this on if you trust the model and what it reads.")
        }
        Flow {
            visible: page.confirmingAutoApprove
            width: parent.width
            spacing: 8
            SettingsPillButton {
                label: qsTr("Auto-approve everything")
                danger: true
                icon: "shield-alert"
                onClicked: {
                    page.confirmingAutoApprove = false
                    panel.setAutoApproveAll(true)
                }
            }
            SettingsPillButton {
                label: qsTr("Cancel")
                onClicked: page.confirmingAutoApprove = false
            }
        }
        Text {
            visible: panel.prefs.buddyPermissionMode === "bypass" && !page.confirmingAutoApprove
            width: parent.width
            text: qsTr("On. Destructive commands, system folders, credential stores and Buddy's own instruction files stay blocked. Switch it off to go back to %1.").arg(panel.modeLabel(panel.modeBeforeAutoApprove))
            color: Theme.warning
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
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
}
