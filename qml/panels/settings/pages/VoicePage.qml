pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Voice")
    subtitle: qsTr("Speak to Buddy, wake it by name, and have answers read back.")
    SettingsSectionHeader {
        title: qsTr("Voice input")
        rightText: Voice.enabled ? (Voice.configured ? qsTr("Ready") : qsTr("Needs setup")) : qsTr("Off")
        description: qsTr("Hold a key or the microphone button and talk. Each clip is turned into text by Deepgram.")
    }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Push-to-talk")
            sub: qsTr("Hold the microphone button in Buddy's bubble (or the shortcut) and speak; release to turn it into text. Audio is recorded only while you hold, sent to Deepgram to be transcribed, kept in memory only and never stored on this computer.")
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
                text: qsTr("Speech to text: %1 (cloud service)").arg(Voice.engine)
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: !Voice.configured ? Voice.problem
                      : Voice.ready ? qsTr("Ready. Change the model and language in the Deepgram page.")
                      : qsTr("Starting…")
                color: Voice.configured ? Theme.textDim : Theme.warning
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
        }
    }

    property var modelWake: ({})
    property var modelTts: ({})

    function refreshModels() {
        modelWake = Wake.detectorModelStatus()
        modelTts = Speech.engineStatuses()
    }

    // The result of adding or removing a wake word, shown under the list.
    property string wakeModelMessage: ""
    property bool wakeModelError: false

    function enabledPhraseNames() {
        const names = []
        const ps = Wake.phrases || []
        for (let i = 0; i < ps.length; ++i)
            if (ps[i].enabled)
                names.push("“" + ps[i].name + "”")
        return names.length > 0 ? names.join(", ") : qsTr("no wake word — switch one on below")
    }

    function addModelChosen(url) {
        const err = Wake.addWakeWordModel(url.toString())
        if (err.length > 0) {
            page.wakeModelMessage = err
            page.wakeModelError = true
            return
        }
        const base = url.toString().split("/").pop().replace(/\.onnx$/i, "").replace(/[_-]+/g, " ")
        page.wakeModelMessage = qsTr("Added “%1”. It is switched on in the list below.").arg(base)
        page.wakeModelError = false
    }

    function removePhrase(id, name) {
        const err = Wake.removeWakeWordModel(id)
        if (err.length > 0) {
            page.wakeModelMessage = err
            page.wakeModelError = true
        } else {
            page.wakeModelMessage = qsTr("Removed “%1”.").arg(name)
            page.wakeModelError = false
        }
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

    readonly property bool allModelsReady: !filesMissing(modelWake.files || [])
                                           && chosenVoiceReady()

    function chosenVoiceReady() {
        const s = modelTts[panel.prefs.buddyTtsEngine]
        return !s || s.ready
    }

    SettingsSectionHeader {
        title: qsTr("Speech models")
        rightText: page.allModelsReady ? qsTr("Ready") : qsTr("Needs attention")
        description: qsTr("The wake-word models on this computer, and whether the spoken-answer voices are set up.")
    }

    SettingsGroupCard {
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
                    { id: "google", name: qsTr("Google AI Studio") },
                    { id: "deepgram", name: qsTr("Deepgram") }
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
        function onPhrasesChanged() { page.refreshModels() }
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
        description: qsTr("Say a wake word instead of clicking. Several phrases can be on at once, including openWakeWord models of your own. The phrase is recognised on this computer; the request you speak after it is turned into text by Deepgram.")
    }

    SettingsGroupCard {
        visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled)
        SettingsToggleRow {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled)
            label: qsTr("Wake word")
            sub: panel.prefs.buddyLiveEnabled
                 ? qsTr("Say a wake word (or follow it straight with what you want) and a live conversation starts. Only the phrase is listened for on this computer; nothing goes to Deepgram until you have said it. After Buddy answers it keeps listening for the time you set in the Deepgram page, then closes the microphone and waits for the phrase again.")
                 : Wake.detector === "speech"
                 ? qsTr("Say the phrase below and Buddy starts listening, then does what you ask and reports back. The microphone stays open while this is on (Windows shows its microphone icon). Whatever is said near it is sent to Deepgram, turned into text, compared with the phrase and forgotten.")
                 : qsTr("Say one of the wake words below and Buddy starts listening, then does what you ask and reports back. The phrase is recognised on this computer; your request is sent to Deepgram to be turned into text. The microphone stays open while this is on (Windows shows its microphone icon).")
            checked: panel.prefs.buddyWakeEnabled
            onToggled: (c) => panel.prefs.buddyWakeEnabled = c
        }

        SettingsLabeledSlider {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled && Wake.detector !== "speech"
            label: qsTr("Sensitivity")
            decimals: 2
            // Left: strict (fewer false wake-ups). Right: easy (wakes on a quieter or less clear phrase).
            value: (0.9 - panel.prefs.buddyWakeThreshold) / 0.7
            onMoved: (v) => panel.prefs.buddyWakeThreshold = Math.round((0.9 - 0.7 * v) * 100) / 100
        }

        Column {
            visible: (panel.prefs.buddyVoiceEnabled || panel.prefs.buddyLiveEnabled) && panel.prefs.buddyWakeEnabled
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: qsTr("Wake words")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: qsTr("Every phrase switched on is listened for at the same time, and the detector reloads as you change them. Your own models are openWakeWord classifiers (a single .onnx per phrase, trained with openWakeWord) — they live in the models folder, on this computer only, and are never uploaded.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: Wake.phrases
                delegate: Column {
                    required property var modelData
                    width: parent.width
                    spacing: 4

                    SettingsToggleRow {
                        label: modelData.name
                        sub: modelData.builtin && modelData.userAdded
                             ? qsTr("Your models · replaces the built-in copy")
                             : modelData.userAdded ? qsTr("Your models") : qsTr("Built in")
                        checked: modelData.enabled
                        onToggled: (c) => Wake.setPhraseEnabled(modelData.id, c)
                    }
                    Flow {
                        visible: modelData.userAdded
                        width: parent.width
                        spacing: 8
                        SettingsPillButton {
                            icon: "trash"
                            label: qsTr("Remove")
                            danger: true
                            onClicked: page.removePhrase(modelData.id, modelData.name)
                        }
                    }
                }
            }
            Text {
                visible: (Wake.phrases || []).length === 0
                width: parent.width
                text: qsTr("No wake word models were found. Add an openWakeWord model below, or drop one into the models folder.")
                color: Theme.warning
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Flow {
                width: parent.width
                spacing: 8
                SettingsPillButton {
                    icon: "plus"
                    label: qsTr("Add wake word…")
                    onClicked: addWakeDialog.open()
                }
            }
            Text {
                visible: page.wakeModelMessage.length > 0
                width: parent.width
                text: page.wakeModelMessage
                color: page.wakeModelError ? Theme.warning : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
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
                        ? qsTr("Listening for %1 on this computer.").arg(page.enabledPhraseNames())
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

    FileDialog {
        id: addWakeDialog
        title: qsTr("Choose an openWakeWord wake word model")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Wake word models (*.onnx)"), qsTr("All files (*)")]
        onAccepted: page.addModelChosen(selectedFile)
    }

    SettingsSectionHeader {
        title: qsTr("Spoken answers")
        rightText: !panel.prefs.buddySpeakReports ? qsTr("Off") : ({ google: qsTr("Google AI Studio"), deepgram: qsTr("Deepgram") })[panel.prefs.buddyTtsEngine] || ""
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
                        { id: "google", name: qsTr("Google AI Studio") },
                        { id: "deepgram", name: qsTr("Deepgram") }
                    ]
                    delegate: SettingsChip {
                        required property var modelData
                        text: modelData.name
                        selected: panel.prefs.buddyTtsEngine === modelData.id
                        onPicked: panel.prefs.buddyTtsEngine = modelData.id
                    }
                }
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


            SettingsLabeledSlider {
                label: qsTr("Speed")
                decimals: 2
                // 0.7 .. 1.5
                value: (panel.prefs.buddyTtsSpeed - 0.7) / 0.8
                onMoved: (v) => panel.prefs.buddyTtsSpeed = Math.round((0.7 + 0.8 * v) * 100) / 100
            }

            Text {
                visible: !Speech.available
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
