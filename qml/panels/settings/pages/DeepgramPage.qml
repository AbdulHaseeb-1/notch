pragma ComponentBehavior: Bound
import QtQuick

// Deepgram: a live, hands-free conversation (Voice Agent), and Deepgram as the speech
// recognizer / voice for the rest of Buddy. Everything the service lets you tune is here.
SettingsPage {
    id: page
    required property var panel
    title: qsTr("Deepgram")
    subtitle: qsTr("Talk to Buddy hands-free, or use Deepgram to hear and speak while Buddy's own model does the thinking.")

    readonly property var prefs: panel.prefs
    readonly property bool flux: Live.flux

    // One "name ... value" line of the conversation numbers.
    component StatRow: Item {
        id: stat
        property string label
        property string value
        width: parent ? parent.width : 300
        height: 22
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: stat.label
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: stat.value
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
        }
    }

    // A draft of the prompt, saved a moment after typing stops, so a running conversation is
    // updated once and not on every key.
    property string promptDraft: prefs.buddyLivePrompt.length > 0 ? prefs.buddyLivePrompt : Live.defaultPrompt
    Timer {
        id: promptSave
        interval: 700
        onTriggered: page.commitPrompt()
    }
    function commitPrompt() {
        promptSave.stop()
        const same = page.promptDraft.trim() === Live.defaultPrompt.trim()
        prefs.buddyLivePrompt = same ? "" : page.promptDraft
    }

    readonly property var providers: [
        { id: "anthropic", name: "Anthropic" },
        { id: "open_ai", name: "OpenAI" },
        { id: "google", name: "Google" },
        { id: "groq", name: "Groq" }
    ]
    readonly property var listenChoices: [
        { id: "nova-3", name: qsTr("Nova 3") },
        { id: "nova-2", name: qsTr("Nova 2") },
        { id: "flux-general-en", name: qsTr("Flux (English)") },
        { id: "flux-general-multi", name: qsTr("Flux (multilingual)") }
    ]
    // The models Deepgram lists for the chosen provider; a few known ones before the list is loaded.
    readonly property var fallbackModels: ({
        anthropic: ["claude-haiku-5-5", "claude-haiku-4-5", "claude-sonnet-5-5", "claude-sonnet-5", "claude-sonnet-4-6"],
        open_ai: ["gpt-4o-mini", "gpt-4.1-mini", "gpt-4.1", "gpt-5-mini"],
        google: ["gemini-2.5-flash", "gemini-2.5-flash-lite"],
        groq: ["openai/gpt-oss-20b"]
    })
    readonly property var providerModels: {
        const out = []
        const list = Live.thinkModels
        for (let i = 0; i < list.length; ++i)
            if (list[i].provider === prefs.buddyLiveProvider)
                out.push(list[i].id)
        return out.length > 0 ? out : (page.fallbackModels[prefs.buddyLiveProvider] || [])
    }
    readonly property string languageBase: String(prefs.buddyLiveLanguage || "en").split("-")[0]
    readonly property var shownVoices: {
        const out = []
        const list = Live.voices
        const lang = page.flux && prefs.buddyLiveListenModel === "flux-general-en" ? "en" : page.languageBase
        for (let i = 0; i < list.length; ++i)
            if (list[i].language === lang)
                out.push(list[i])
        return out.length > 0 ? out : list
    }

    Component.onCompleted: {
        if (deepgramKeyOk() && Live.thinkModels.length === 0 && !Live.modelsLoading)
            Live.refreshModels()
    }
    function deepgramKeyOk() { return String(prefs.buddyDgKey).trim().length >= 20 }

    function stateText() {
        switch (Live.state) {
        case "connecting": return qsTr("Connecting…")
        case "listening": return Live.muted ? qsTr("Connected. The microphone is muted.") : qsTr("Listening")
        case "thinking": return qsTr("Thinking…")
        case "speaking": return qsTr("Speaking")
        default: return qsTr("Not running")
        }
    }

    // ---- account ---------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Account")
        rightText: Live.testState === "ok" ? qsTr("Working")
                   : page.deepgramKeyOk() ? qsTr("Key added") : qsTr("Needs a key")
        description: qsTr("One Deepgram API key covers the conversation, speech to text and the voice. It stays on this computer and is only sent to Deepgram.")
    }
    SettingsGroupCard {
        SettingsFieldBox {
            width: parent.width
            label: qsTr("Deepgram API key")
            placeholder: qsTr("Paste your API key")
            hint: qsTr("Create one at console.deepgram.com. New accounts include free credit.")
            secret: true
            mono: true
            text: page.prefs.buddyDgKey
            onEdited: (v) => page.prefs.buddyDgKey = v.trim()
        }
        Flow {
            width: parent.width
            spacing: 8
            SettingsPillButton {
                primary: true
                icon: "bolt"
                label: Live.testState === "testing" ? qsTr("Testing…") : qsTr("Test connection")
                enabled: Live.testState !== "testing"
                onClicked: Live.test()
            }
            SettingsPillButton {
                icon: "reset"
                label: Live.modelsLoading ? qsTr("Loading…") : qsTr("Reload models and voices")
                enabled: !Live.modelsLoading && page.deepgramKeyOk()
                onClicked: Live.refreshModels()
            }
        }
        Text {
            visible: Live.testState !== "" || Live.modelsProblem.length > 0
            width: parent.width
            text: Live.testState !== "" ? Live.testMessage : Live.modelsProblem
            color: Live.testState === "ok" ? Theme.success
                   : Live.testState === "testing" ? Theme.textDim : Theme.warning
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
        Text {
            visible: Live.testState === "" && Live.modelsProblem.length === 0
            width: parent.width
            text: qsTr("“Test connection” opens a short session with your settings and asks the model one question, so a wrong model name or voice shows up here and not in the middle of a conversation. It costs a fraction of a cent.")
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
    }

    // ---- live conversation -------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Live conversation")
        rightText: !page.prefs.buddyLiveEnabled ? qsTr("Off") : (Live.active ? page.stateText() : (Live.problem.length > 0 ? qsTr("Needs setup") : qsTr("Ready")))
        description: qsTr("Deepgram listens, thinks and answers out loud in one continuous call. You can interrupt it, and it can hand tasks to Buddy.")
    }
    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Live conversation")
            sub: qsTr("Adds a conversation button to Buddy and makes the voice shortcut start and end a conversation. Your microphone is open, and what you say goes to Deepgram, only while a conversation is running.")
            checked: page.prefs.buddyLiveEnabled
            onToggled: (c) => page.prefs.buddyLiveEnabled = c
        }

        Column {
            visible: page.prefs.buddyLiveEnabled
            width: parent.width
            spacing: 12

            SettingsCardDivider {}

            Flow {
                width: parent.width
                spacing: 8
                SettingsPillButton {
                    primary: !Live.active
                    danger: Live.active
                    icon: Live.active ? "stop" : "mic"
                    label: Live.active ? qsTr("End conversation") : qsTr("Start conversation")
                    enabled: Live.active || Live.problem.length === 0
                    onClicked: Live.toggle()
                }
                SettingsPillButton {
                    visible: Live.active
                    icon: Live.muted ? "mic" : "mic-off"
                    label: Live.muted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: Live.muted = !Live.muted
                }
                SettingsPillButton {
                    visible: Live.active && Live.speaking
                    icon: "stop"
                    label: qsTr("Interrupt")
                    onClicked: Live.interrupt()
                }
            }
            Text {
                width: parent.width
                text: Live.active ? page.stateText()
                      : Live.problem.length > 0 ? Live.problem
                      : qsTr("Shortcut: %1 starts and ends a conversation.").arg(TalkKey.shortcut.length > 0 ? TalkKey.shortcut : "Ctrl+Alt+V")
                color: !Live.active && Live.problem.length > 0 ? Theme.warning : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Text {
                visible: Live.error.length > 0
                width: parent.width
                text: Live.error
                color: Theme.warning
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Text {
                visible: Live.notice.length > 0 && Live.active
                width: parent.width
                text: Live.notice
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            Column {
                visible: Live.active && (Live.userText.length > 0 || Live.agentText.length > 0)
                width: parent.width
                spacing: 4
                Text {
                    visible: Live.userText.length > 0
                    width: parent.width
                    text: qsTr("You: %1").arg(Live.userText)
                    color: Theme.inkSoft
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: Live.agentText.length > 0
                    width: parent.width
                    text: qsTr("Buddy: %1").arg(Live.agentText)
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: Live.latencyMs > 0
                    text: qsTr("Last reply: %1 ms").arg(Live.latencyMs)
                    color: Theme.textFaint
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                }
            }
            SettingsNotice {
                visible: Live.restartNeeded && Live.active
                text: qsTr("Some changes apply from the next conversation. The prompt, voice and speed already took effect.")
            }

            SettingsCardDivider {}

            Text {
                text: qsTr("Audio")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
            }
            Segmented {
                width: parent.width
                options: [{ label: qsTr("Speakers"), value: "speakers" }, { label: qsTr("Headphones"), value: "headphones" }]
                value: page.prefs.buddyLiveAudioMode
                onPicked: (v) => page.prefs.buddyLiveAudioMode = v
            }
            Text {
                width: parent.width
                text: page.prefs.buddyLiveAudioMode === "headphones"
                      ? qsTr("The microphone stays open while Buddy talks, so you can interrupt it by speaking. Use this with headphones: through speakers it would hear itself.")
                      : qsTr("The microphone is held back while Buddy talks, so it does not hear itself through your speakers. Speak up over it to cut in, or use Interrupt.")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }
            SettingsToggleRow {
                visible: page.prefs.buddyLiveAudioMode === "speakers"
                label: qsTr("Cancel echo")
                sub: qsTr("Takes Buddy's own voice out of the microphone, so speaking over it is heard reliably and the first words of your interruption are kept. It learns your room while Buddy talks, in a few seconds. Headphones need none of this.")
                checked: page.prefs.buddyLiveEchoCancel
                onToggled: (c) => page.prefs.buddyLiveEchoCancel = c
            }

            Text {
                text: qsTr("After Buddy answers, keep listening for")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
            }
            Segmented {
                width: parent.width
                options: [{ label: qsTr("30 s"), value: 30 }, { label: qsTr("1 min"), value: 60 },
                          { label: qsTr("2 min"), value: 120 }, { label: qsTr("5 min"), value: 300 },
                          { label: qsTr("Wake word"), value: 0 }]
                value: page.prefs.buddyLiveFollowUp
                onPicked: (v) => page.prefs.buddyLiveFollowUp = v
            }
            Text {
                width: parent.width
                text: page.prefs.buddyLiveFollowUp > 0
                      ? qsTr("Buddy stays on the line so you can follow up without repeating its name. When it has been quiet this long the conversation ends, the microphone closes and nothing more is sent to Deepgram.")
                      : qsTr("The conversation ends right after Buddy has answered (a few seconds' grace, longer if it asked you something). Say the wake word, \"Hey Buddy\", to start the next one. Turn the wake word on in Voice settings.")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
            }

            SettingsToggleRow {
                label: qsTr("Soft sound while Buddy thinks")
                sub: qsTr("A quiet cue when an answer takes more than a moment, so a short pause does not feel like the line went dead.")
                checked: page.prefs.buddyLiveThinkCue
                onToggled: (c) => page.prefs.buddyLiveThinkCue = c
            }

            SettingsToggleRow {
                label: qsTr("Let Buddy do things")
                sub: qsTr("The voice agent can hand tasks (open apps, music, files, web, mouse and keyboard, reminders) to Buddy's own agent. They run with your usual safety settings, and you still approve what needs approval.")
                checked: page.prefs.buddyLiveAskBuddy
                onToggled: (c) => page.prefs.buddyLiveAskBuddy = c
            }

            // How the conversation is going: the current one, or the last.
            Column {
                visible: Live.active || Live.replies > 0 || Live.stalls > 0
                width: parent.width
                spacing: 4

                SettingsCardDivider {}

                Text {
                    text: qsTr("How it is going")
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                    bottomPadding: 4
                }
                StatRow {
                    label: qsTr("Until you hear Buddy")
                    value: Live.responseDelayMs > 0
                           ? qsTr("%1 ms (average %2 ms)").arg(Live.responseDelayMs).arg(Live.avgResponseDelayMs) : "-"
                }
                StatRow {
                    label: qsTr("Deepgram's answer time")
                    value: Live.latencyMs > 0
                           ? qsTr("%1 ms (average %2 ms)").arg(Live.latencyMs).arg(Live.avgLatencyMs) : "-"
                }
                StatRow {
                    label: qsTr("Playback buffer")
                    value: qsTr("%1 ms").arg(Live.bufferMs)
                }
                StatRow {
                    label: qsTr("Stalls this conversation")
                    value: String(Live.stalls)
                }
                StatRow {
                    visible: page.prefs.buddyLiveAudioMode === "speakers" && page.prefs.buddyLiveEchoCancel
                    label: qsTr("Echo removed")
                    value: Live.echoReductionDb < 3 ? qsTr("learning") : qsTr("%1 dB").arg(Math.round(Live.echoReductionDb))
                }
                Text {
                    width: parent.width
                    topPadding: 4
                    text: qsTr("The buffer grows a little after the connection stalls and shrinks back when it runs clean. Many stalls mean a weak connection.")
                    color: Theme.textFaint
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---- hearing -------------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Hearing")
        rightText: page.flux ? qsTr("Flux") : qsTr("Nova")
        description: qsTr("How Deepgram turns your voice into words, in the conversation and for push-to-talk.")
    }
    SettingsGroupCard {
        Text {
            text: qsTr("Model")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.listenChoices
                delegate: SettingsChip {
                    required property var modelData
                    text: modelData.name
                    selected: page.prefs.buddyLiveListenModel === modelData.id
                    onPicked: page.prefs.buddyLiveListenModel = modelData.id
                }
            }
        }
        Text {
            width: parent.width
            text: page.flux
                  ? qsTr("Flux decides for itself when you have finished speaking, which makes the conversation feel faster. For push-to-talk clips Noch uses Nova 3, since Flux only streams.")
                  : qsTr("Nova is the most accurate. Deepgram waits for a pause to decide you are done speaking.")
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
        SettingsFieldBox {
            visible: !page.flux
            width: parent.width
            label: qsTr("Language")
            placeholder: "en"
            hint: qsTr("A language code such as en, es, fr, de, hi, or multi for several at once.")
            mono: true
            text: page.prefs.buddyLiveLanguage
            onEdited: (v) => page.prefs.buddyLiveLanguage = v.trim()
        }
        SettingsFieldBox {
            width: parent.width
            label: qsTr("Words to listen for")
            placeholder: qsTr("Noch, Moonshine, your name")
            hint: qsTr("Names and jargon Deepgram should get right, separated by commas.")
            text: page.prefs.buddyLiveKeyterms
            onEdited: (v) => page.prefs.buddyLiveKeyterms = v
        }
        SettingsLabeledSlider {
            visible: page.flux
            label: qsTr("How sure it must be that you finished")
            display: Number(page.prefs.buddyLiveEot).toFixed(2)
            // 0.5 .. 1.0
            value: (page.prefs.buddyLiveEot - 0.5) / 0.5
            onMoved: (v) => page.prefs.buddyLiveEot = Math.round((0.5 + 0.5 * v) * 100) / 100
        }
        Text {
            visible: page.flux
            width: parent.width
            text: qsTr("Lower answers sooner but may cut you off; higher waits longer for a pause. Applies from the next conversation.")
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }

        SettingsCardDivider {}

        Text {
            text: qsTr("Push-to-talk and the wake word")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        Segmented {
            width: parent.width
            options: [{ label: qsTr("On this computer"), value: "local" }, { label: qsTr("Deepgram"), value: "deepgram" }]
            value: page.prefs.buddySttEngine
            onPicked: (v) => page.prefs.buddySttEngine = v
        }
        Text {
            width: parent.width
            text: page.prefs.buddySttEngine === "deepgram"
                  ? (Voice.configured
                     ? qsTr("Each push-to-talk clip is sent to Deepgram for transcription, and Buddy's own model answers. The always-on wake word keeps using the detector on this computer, so nothing is streamed until you say it.")
                     : Voice.problem)
                  : qsTr("Moonshine runs on this computer: nothing you say leaves it. Slower to start and English only.")
            color: page.prefs.buddySttEngine === "deepgram" && !Voice.configured ? Theme.warning : Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
    }

    // ---- thinking ----------------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Thinking")
        rightText: page.prefs.buddyLiveModel
        description: qsTr("The language model that answers in a live conversation. Deepgram runs it for you; you can also point it at your own.")
    }
    SettingsGroupCard {
        Text {
            text: qsTr("Provider")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.providers
                delegate: SettingsChip {
                    required property var modelData
                    text: modelData.name
                    selected: page.prefs.buddyLiveProvider === modelData.id
                    onPicked: {
                        page.prefs.buddyLiveProvider = modelData.id
                        // Keep a model that belongs to the provider.
                        const models = (Live.thinkModels.length > 0
                                        ? Live.thinkModels.filter((m) => m.provider === modelData.id).map((m) => m.id)
                                        : page.fallbackModels[modelData.id]) || []
                        if (models.length > 0 && models.indexOf(page.prefs.buddyLiveModel) < 0)
                            page.prefs.buddyLiveModel = models[0]
                    }
                }
            }
        }
        SettingsFieldBox {
            width: parent.width
            label: qsTr("Model")
            placeholder: "claude-haiku-5-5"
            hint: qsTr("Any model name Deepgram lists for this provider, or your own endpoint's model name.")
            mono: true
            text: page.prefs.buddyLiveModel
            onEdited: (v) => page.prefs.buddyLiveModel = v.trim()
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.providerModels
                delegate: SettingsChip {
                    required property string modelData
                    text: modelData
                    selected: page.prefs.buddyLiveModel === modelData
                    onPicked: page.prefs.buddyLiveModel = modelData
                }
            }
        }

        SettingsToggleRow {
            visible: Live.temperatureApplies
            label: qsTr("Model's default temperature")
            sub: qsTr("Temperature makes answers more or less varied. Leave this on unless you want to tune it.")
            checked: page.prefs.buddyLiveTemperature < 0
            onToggled: (c) => page.prefs.buddyLiveTemperature = c ? -1 : 0.7
        }
        SettingsLabeledSlider {
            visible: Live.temperatureApplies && page.prefs.buddyLiveTemperature >= 0
            label: qsTr("Temperature")
            decimals: 2
            value: Math.max(0, page.prefs.buddyLiveTemperature) / (page.prefs.buddyLiveProvider === "anthropic" ? 1.0 : 2.0)
            onMoved: (v) => page.prefs.buddyLiveTemperature = Math.round(v * (page.prefs.buddyLiveProvider === "anthropic" ? 1.0 : 2.0) * 100) / 100
        }
        Text {
            visible: !Live.temperatureApplies
            width: parent.width
            text: qsTr("This model manages its own temperature, so there is nothing to set here.")
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }

        SettingsCardDivider {}

        SettingsToggleRow {
            label: qsTr("Quick replies")
            sub: qsTr("Keeps answers to a sentence or two and hands anything that needs thought, lookup or work to Buddy, so the voice starts and finishes sooner. A small, fast model suits this best; a bigger one mostly adds delay on a voice call. Applies right away.")
            checked: page.prefs.buddyLiveQuick
            onToggled: (c) => page.prefs.buddyLiveQuick = c
        }

        SettingsCardDivider {}

        SettingsTextArea {
            width: parent.width
            label: qsTr("Instructions")
            boxHeight: 150
            hint: qsTr("How the voice agent should behave. It is told to hand real tasks to Buddy, so keep the part about ask_buddy if you want that. Changes apply right away, even in a running conversation.")
            text: page.promptDraft
            onEdited: (v) => {
                page.promptDraft = v
                promptSave.restart()
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            SettingsPillButton {
                icon: "reset"
                label: qsTr("Reset to default")
                enabled: page.prefs.buddyLivePrompt.length > 0 || page.promptDraft !== Live.defaultPrompt
                onClicked: {
                    promptSave.stop()
                    page.promptDraft = Live.defaultPrompt
                    page.prefs.buddyLivePrompt = ""
                }
            }
            Text {
                text: qsTr("%1 characters").arg(page.promptDraft.length)
                color: page.promptDraft.length > 25000 ? Theme.warning : Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                height: 28
                verticalAlignment: Text.AlignVCenter
            }
        }
        SettingsFieldBox {
            width: parent.width
            label: qsTr("Greeting")
            placeholder: qsTr("Hi, I'm Buddy. What can I do?")
            hint: qsTr("Spoken when a conversation starts. A short one fills the quiet while the call connects; leave it empty to wait for you.")
            text: page.prefs.buddyLiveGreeting
            onEdited: (v) => page.prefs.buddyLiveGreeting = v
        }

        SettingsCardDivider {}

        Column {
            id: ownModel
            property bool open: page.prefs.buddyLiveEndpoint.length > 0
            width: parent.width
            spacing: 10
            SettingsToggleRow {
                label: qsTr("Use your own model")
                sub: qsTr("Send the conversation to your own endpoint (for example Anthropic's API with your key, or any OpenAI-compatible server) instead of Deepgram's managed model.")
                checked: ownModel.open
                onToggled: (c) => {
                    ownModel.open = c
                    if (!c) {
                        page.prefs.buddyLiveEndpoint = ""
                        page.prefs.buddyLiveEndpointKey = ""
                    }
                }
            }
            SettingsFieldBox {
                visible: ownModel.open
                width: parent.width
                label: qsTr("Endpoint address")
                placeholder: page.prefs.buddyLiveProvider === "anthropic" ? "https://api.anthropic.com/v1/messages"
                                                                         : "https://api.openai.com/v1/chat/completions"
                hint: qsTr("Must be https and reachable from the internet: Deepgram's servers call it, not this computer.")
                mono: true
                text: page.prefs.buddyLiveEndpoint
                onEdited: (v) => page.prefs.buddyLiveEndpoint = v.trim()
            }
            SettingsFieldBox {
                visible: ownModel.open
                width: parent.width
                label: qsTr("API key for your model")
                placeholder: qsTr("Paste your API key")
                hint: qsTr("Sent to Deepgram, which forwards it to your endpoint.")
                secret: true
                mono: true
                text: page.prefs.buddyLiveEndpointKey
                onEdited: (v) => page.prefs.buddyLiveEndpointKey = v.trim()
            }
        }
    }

    // ---- speaking ------------------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Speaking")
        description: qsTr("The voice Buddy answers with in the conversation, and when Deepgram is chosen as the voice in Voice settings.")
    }
    SettingsGroupCard {
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.shownVoices
                delegate: SettingsChip {
                    required property var modelData
                    text: modelData.label
                    selected: page.prefs.buddyLiveVoice === modelData.id
                    onPicked: page.prefs.buddyLiveVoice = modelData.id
                }
            }
        }
        SettingsFieldBox {
            width: parent.width
            label: qsTr("Voice")
            placeholder: "aura-2-thalia-en"
            hint: qsTr("Pick one above, or type a Deepgram voice name.")
            mono: true
            text: page.prefs.buddyLiveVoice
            onEdited: (v) => page.prefs.buddyLiveVoice = v.trim()
        }
        SettingsLabeledSlider {
            label: qsTr("Speed")
            decimals: 2
            // 0.7 .. 1.5
            value: (page.prefs.buddyLiveSpeed - 0.7) / 0.8
            onMoved: (v) => page.prefs.buddyLiveSpeed = Math.round((0.7 + 0.8 * v) * 100) / 100
        }
        SettingsLabeledSlider {
            label: qsTr("Volume")
            display: Math.round(page.prefs.buddyLiveVolume * 100) + "%"
            // 0 .. 1.5
            value: page.prefs.buddyLiveVolume / 1.5
            onMoved: (v) => page.prefs.buddyLiveVolume = Math.round(v * 1.5 * 100) / 100
        }
        Flow {
            width: parent.width
            spacing: 8
            SettingsPillButton {
                icon: "volume"
                label: Speech.speaking ? qsTr("Stop") : qsTr("Preview voice")
                enabled: Speech.speaking || page.deepgramKeyOk()
                onClicked: Speech.speaking ? Speech.stop() : Speech.previewDeepgram()
            }
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
    }

    // ---- what each setting costs / does ---------------------------------------------------------------
    SettingsNotice {
        text: qsTr("Deepgram bills the conversation by the minute, including its language model. Plain speech to text and voice are billed by the second of audio and the character.")
    }
}
