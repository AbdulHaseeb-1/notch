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
        description: Voice.engineIsLocal
                     ? qsTr("Hold a key or the microphone button and talk. Speech is turned into text on this computer by Moonshine.")
                     : qsTr("Hold a key or the microphone button and talk. Each clip is turned into text by Deepgram.")
    }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Push-to-talk")
            sub: Voice.engineIsLocal
                 ? qsTr("Hold the microphone button in Buddy's bubble (or the shortcut) and speak; release to turn it into text. Audio is recorded only while you hold and turned into text by a model on this computer. It is kept in memory only, never sent anywhere and never stored.")
                 : qsTr("Hold the microphone button in Buddy's bubble (or the shortcut) and speak; release to turn it into text. Audio is recorded only while you hold, sent to Deepgram to be transcribed, kept in memory only and never stored on this computer.")
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
                text: Voice.engineIsLocal ? qsTr("Speech to text: %1, on this computer").arg(Voice.engine)
                                          : qsTr("Speech to text: %1 (cloud service)").arg(Voice.engine)
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: !Voice.configured ? Voice.problem
                      : Voice.ready ? (Voice.engineIsLocal ? qsTr("Ready. English only. Nothing you say leaves this computer.")
                                                           : qsTr("Ready. Change the engine, model and language in the Deepgram page."))
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
                    visible: Voice.engineIsLocal
                    icon: "folder"
                    label: qsTr("Open models folder")
                    onClicked: Voice.openModelFolder()
                }
            }
        }
    }

    property var modelStt: ({})
    property var modelWake: ({})
    property var modelTts: ({})

    function refreshModels() {
        modelStt = Voice.sttModelStatus()
        modelWake = Wake.detectorModelStatus()
        modelTts = Speech.engineStatuses()
    }

    function filesMissing(files) {
        for (let i = 0; i < files.length; ++i)
            if (!files[i].found)
                return true
        return false
    }

    function fileSizeText(bytes, found) {
        if (!found)
            return qsTr("missing")
        if (bytes > 0)
            return (bytes / 1048576).toFixed(1) + qsTr(" MB")
        return qsTr("built in")
    }

    readonly property bool allModelsReady: !filesMissing(modelStt.files || [])
                                           && modelTts.system && modelTts.system.ready

    SettingsSectionHeader {
        title: qsTr("Speech models")
        rightText: page.allModelsReady ? qsTr("Ready") : qsTr("Needs attention")
        description: qsTr("The speech models on this computer and whether they are correctly configured.")
    }

    SettingsGroupCard {
        Column {
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: qsTr("Moonshine Tiny (speech to text)")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: page.modelStt.ready
                      ? qsTr("Ready. English only. Nothing you say leaves this computer.")
                      : (page.modelStt.problem || qsTr("The model files are not all here."))
                color: page.modelStt.ready ? Theme.textDim : Theme.warning
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: page.modelStt.files || []
                delegate: Text {
                    required property var modelData
                    width: parent.width
                    text: modelData.name + " · " + page.fileSizeText(modelData.bytes, modelData.found)
                    color: modelData.found ? Theme.textDim : Theme.warning
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                }
            }
            SettingsPillButton {
                icon: "folder"
                label: qsTr("Open models folder")
                onClicked: Voice.openModelFolder()
            }
        }

        SettingsCardDivider {}

        Column {
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: qsTr("Wake detector (openWakeWord)")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: page.modelWake.detector === "local"
                      ? qsTr("The phrase is found on this computer.")
                      : page.modelWake.detector === "loading" ? qsTr("Starting…")
                      : qsTr("No local model: sentences are transcribed and compared with the phrase instead.")
                color: page.modelWake.detector === "speech" ? Theme.warning : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Repeater {
                visible: page.modelWake.detector !== "speech"
                model: page.modelWake.files || []
                delegate: Text {
                    required property var modelData
                    width: parent.width
                    text: modelData.name + " · " + page.fileSizeText(modelData.bytes, modelData.found)
                    color: modelData.found ? Theme.textDim : Theme.warning
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                }
            }
            SettingsPillButton {
                icon: "folder"
                label: qsTr("Open models folder")
                onClicked: Wake.openModelFolder()
            }
        }

        SettingsCardDivider {}

        Column {
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: qsTr("Spoken answers (text to speech)")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
            }
            Repeater {
                model: [
                    { id: "system", name: qsTr("This computer") },
                    { id: "google", name: qsTr("Google AI Studio") },
                    { id: "kokoro", name: qsTr("Kokoro") }
                ]
                delegate: Text {
                    required property var modelData
                    readonly property var s: page.modelTts[modelData.id]
                    width: parent.width
                    text: (s && s.ready ? "✓ " : "! ") + modelData.name + " — " + (s ? s.detail : "")
                    color: s && s.ready ? Theme.textDim : Theme.warning
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Connections {
        target: Voice
        function onReadyChanged() { page.refreshModels() }
        function onConfigChanged() { page.refreshModels() }
    }
    Connections {
        target: Wake
        function onStateChanged() { page.refreshModels() }
        function onDetectorChanged() { page.refreshModels() }
    }
    Connections {
        target: Speech
        function onConfigChanged() { page.refreshModels() }
        function onErrorChanged() { page.refreshModels() }
    }
    Component.onCompleted: page.refreshModels()

    SettingsSectionHeader {
        visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled)
        title: qsTr("Wake word")
        rightText: !panel.prefs.buddyWakeEnabled ? qsTr("Off") : (Wake.state === "listening" ? qsTr("Listening") : (Wake.state === "awaiting" ? qsTr("Heard you") : qsTr("On")))
        description: qsTr("Say “Hey Buddy” instead of clicking. The phrase and your request are both recognised on this computer.")
    }

    SettingsGroupCard {
        visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled)
        SettingsToggleRow {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled)
            label: qsTr("Wake word")
            sub: panel.prefs.buddyLiveEnabled
                 ? qsTr("Say “Hey Buddy” (or “Hey Buddy, open Notepad”) and a live conversation starts. Only the phrase is listened for on this computer; nothing goes to Deepgram until you have said it. After Buddy answers it keeps listening for the time you set in the Deepgram page, then closes the microphone and waits for the phrase again.")
                 : Wake.detector === "speech"
                 ? qsTr("Say the phrase below and Buddy starts listening, then does what you ask and reports back. The microphone stays open while this is on (Windows shows its microphone icon). Whatever is said near it is turned into text on this computer, compared with the phrase and forgotten.")
                 : qsTr("Say “Hey Buddy” and Buddy starts listening, then does what you ask and reports back. The phrase and your request are both recognised on this computer: nothing is sent anywhere. The microphone stays open while this is on (Windows shows its microphone icon).")
            checked: panel.prefs.buddyWakeEnabled
            onToggled: (c) => panel.prefs.buddyWakeEnabled = c
        }

        SettingsLabeledSlider {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled && Wake.detector !== "speech"
            label: qsTr("Sensitivity")
            decimals: 2
            // Left: strict (fewer false wake-ups). Right: easy (wakes on a quieter or less clear “Hey Buddy”).
            value: (0.9 - panel.prefs.buddyWakeThreshold) / 0.7
            onMoved: (v) => panel.prefs.buddyWakeThreshold = Math.round((0.9 - 0.7 * v) * 100) / 100
        }

        Flow {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled && Wake.detector !== "speech"
            width: parent.width
            spacing: 8
            SettingsPillButton {
                icon: "folder"
                label: qsTr("Open models folder")
                onClicked: Wake.openModelFolder()
            }
        }

        SettingsFieldBox {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled && Wake.detector === "speech"
            width: parent.width
            label: qsTr("Wake phrase")
            placeholder: "hey buddy"
            hint: qsTr("Two or more syllables work best; used at the start of a sentence.")
            text: panel.prefs.buddyWakePhrase
            onEdited: (v) => panel.prefs.buddyWakePhrase = v
        }

        Text {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled
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
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled && Wake.localProblem.length > 0
            width: parent.width
            text: qsTr("Local detector unavailable: %1").arg(Wake.localProblem)
            color: Theme.warning
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
    }

    SettingsSectionHeader {
        title: qsTr("Spoken answers")
        rightText: !panel.prefs.buddySpeakReports ? qsTr("Off") : ({ system: qsTr("This computer"), google: qsTr("Google AI Studio"), kokoro: qsTr("Kokoro") })[panel.prefs.buddyTtsEngine] || ""
        description: qsTr("Buddy reads a short summary of each answer out loud: never code or links.")
    }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Read answers aloud")
            sub: qsTr("When a request started by voice finishes, needs your approval or fails, Buddy says a short summary out loud. Never code or links.")
            checked: panel.prefs.buddySpeakReports
            onToggled: (c) => panel.prefs.buddySpeakReports = c
        }

        Column {
            visible: panel.prefs.buddySpeakReports
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
                        { id: "google", name: qsTr("Google AI Studio") },
                        { id: "deepgram", name: qsTr("Deepgram") },
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
                    label: qsTr("Google AI Studio API key")
                    placeholder: qsTr("Paste your API key")
                    hint: qsTr("Create a key at aistudio.google.com/apikey. Uses Gemini TTS; the spoken text is sent to Google. Stored locally in Noch settings.")
                    secret: true
                    mono: true
                    text: panel.prefs.buddyTtsGoogleKey
                    onEdited: (v) => panel.prefs.buddyTtsGoogleKey = v
                }
                SettingsFieldBox {
                    width: parent.width
                    label: qsTr("Voice")
                    placeholder: "Kore"
                    hint: qsTr("A Gemini TTS voice name, such as Kore, Puck, Zephyr or Charon.")
                    mono: true
                    text: panel.prefs.buddyTtsGoogleVoice
                    onEdited: (v) => panel.prefs.buddyTtsGoogleVoice = v
                }
            }

            Column {
                visible: panel.prefs.buddyTtsEngine === "deepgram"
                width: parent.width
                spacing: 6
                Text {
                    width: parent.width
                    text: Speech.available
                          ? qsTr("Deepgram Aura voice %1. The key and the voice are set in the Deepgram page; the text is sent to Deepgram.").arg(panel.prefs.buddyLiveVoice)
                          : Speech.problem
                    color: Speech.available ? Theme.textDim : Theme.warning
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
                Flow {
                    width: parent.width
                    SettingsPillButton {
                        icon: "settings"
                        label: qsTr("Open Deepgram settings")
                        onClicked: panel.page = "deepgram"
                    }
                }
            }

            Column {
                id: kokoroSettings
                visible: panel.prefs.buddyTtsEngine === "kokoro"
                width: parent.width
                spacing: 10

                function loadVoices() {
                    if (visible && Speech.kokoroVoices.length === 0 && !Speech.kokoroVoicesLoading)
                        Speech.refreshKokoroVoices()
                }
                Component.onCompleted: loadVoices()
                onVisibleChanged: loadVoices()

                Timer {
                    id: voicesRetry
                    interval: 700
                    onTriggered: Speech.refreshKokoroVoices()
                }

                SettingsFieldBox {
                    width: parent.width
                    label: qsTr("Kokoro server")
                    placeholder: "http://127.0.0.1:8880"
                    hint: qsTr("A Kokoro TTS server running on this computer (for example Kokoro-FastAPI). Speech is only sent to this computer or over https.")
                    mono: true
                    text: panel.prefs.buddyTtsKokoroBase
                    onEdited: (v) => {
                        panel.prefs.buddyTtsKokoroBase = v
                        voicesRetry.restart()
                    }
                    onAccepted: Speech.refreshKokoroVoices()
                }
                SettingsFieldBox {
                    width: parent.width
                    label: qsTr("Voice")
                    placeholder: "af_heart"
                    hint: qsTr("Type any voice name, or pick one from the server's list below.")
                    mono: true
                    text: panel.prefs.buddyTtsKokoroVoice
                    onEdited: (v) => panel.prefs.buddyTtsKokoroVoice = v
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Text {
                        width: Math.max(0, parent.width - refreshVoices.width - parent.spacing)
                        anchors.verticalCenter: parent.verticalCenter
                        text: Speech.kokoroVoicesLoading
                              ? qsTr("Looking for voices on this server…")
                              : Speech.kokoroVoicesProblem.length > 0
                                ? Speech.kokoroVoicesProblem
                                : Speech.kokoroVoices.length > 0
                                  ? qsTr("%1 voices available. Pick one below, or type another name.").arg(Speech.kokoroVoices.length)
                                  : qsTr("Load the voices this server offers.")
                        color: Speech.kokoroVoicesProblem.length > 0 ? Theme.warning : Theme.textFaint
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        wrapMode: Text.WordWrap
                    }
                    SettingsPillButton {
                        id: refreshVoices
                        icon: "reset"
                        label: Speech.kokoroVoicesLoading ? qsTr("Loading…") : qsTr("Refresh")
                        enabled: !Speech.kokoroVoicesLoading
                        onClicked: Speech.refreshKokoroVoices()
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 8
                    visible: Speech.kokoroVoices.length > 0
                    Repeater {
                        model: Speech.kokoroVoices
                        delegate: SettingsChip {
                            required property string modelData
                            text: modelData
                            selected: panel.prefs.buddyTtsKokoroVoice === modelData
                            onPicked: panel.prefs.buddyTtsKokoroVoice = modelData
                        }
                    }
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
