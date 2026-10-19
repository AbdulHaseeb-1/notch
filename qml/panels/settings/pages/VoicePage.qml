pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Voice")
    subtitle: qsTr("Speak to Buddy, wake it by name, and have answers read back.")
    SettingsSectionHeader {
        title: qsTr("Voice input")
        rightText: Voice.enabled ? (Voice.configured ? qsTr("Ready") : qsTr("Needs setup")) : qsTr("Off")
        description: qsTr("Hold a key or the microphone button and talk. Speech is turned into text on this computer by Moonshine.")
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
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyVoiceEnabled
        title: qsTr("Wake word")
        rightText: !panel.prefs.buddyWakeEnabled ? qsTr("Off") : (Wake.state === "listening" ? qsTr("Listening") : (Wake.state === "awaiting" ? qsTr("Heard you") : qsTr("On")))
        description: qsTr("Say “Hey Buddy” instead of clicking. The phrase and your request are both recognised on this computer.")
    }

    SettingsGroupCard {
        visible: panel.prefs.buddyVoiceEnabled
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
    }

    SettingsSectionHeader {
        visible: panel.prefs.buddyVoiceEnabled
        title: qsTr("Spoken answers")
        rightText: !panel.prefs.buddySpeakReports ? qsTr("Off") : ({ system: qsTr("This computer"), google: qsTr("Google Cloud"), kokoro: qsTr("Kokoro") })[panel.prefs.buddyTtsEngine] || ""
        description: qsTr("Buddy reads a short summary of each answer out loud: never code or links.")
    }

    SettingsGroupCard {
        visible: panel.prefs.buddyVoiceEnabled
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
