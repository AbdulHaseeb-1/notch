pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("AI model")
    subtitle: qsTr("Which provider answers Buddy, and which model it uses.")

    SettingsNotice {
        visible: !panel.realChat
        icon: "cpu"
        text: qsTr("AI chat is off, so answers come from Buddy's built-in local routes. Turn it on to choose a provider and a model.")
        action: qsTr("Turn on")
        onActivated: panel.setRealChat(true)
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
                    { label: panel.labelFor("opencode"), value: "opencode" },
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
                hint: panel.infoFor(panel.prefs.buddyProvider).keyHint
                      || (panel.infoFor(panel.prefs.buddyProvider).needsKey
                      ? qsTr("Stored locally in Noch settings.")
                      : qsTr("Leave blank if not required."))
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
}
