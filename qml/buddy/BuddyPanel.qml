pragma ComponentBehavior: Bound
import QtQuick

// The buddy's result card: a repositionable, resizable companion panel that
// hosts AI chat responses with live streaming reveal, expressive mini-avatar
// reactions, inline follow-up input, and local task cards (chart, research,
// tasks, capability list).
Item {
    id: panel

    property bool open: false
    property string kind: "help"          // chat | chart | research | tasks | help
    property string title: ""
    property string userPrompt: ""
    property string phase: "ready"        // ready | loading | error
    property string errorMessage: ""
    property var payload: null            // task-specific data from BuddyRouter / BuddyProvider
    property bool simulated: true

    // Real desktop bounds for clamping (usually root.contentItem).
    property var desktop: null

    signal dismissed()
    signal retryRequested()
    signal newRequestRequested()
    signal followUpSubmitted(string text)
    signal continueRequested()            // "Continue" after a desktop-control pause
    signal loved()
    signal speakingTick()
    signal planEdited(var items)
    signal capability(string id)


    readonly property real pad: 14
    readonly property real headerH: 48
    readonly property real footerH: kind === "chat" ? 50 : 42
    readonly property real width_: {
        const avail = (desktop ? desktop.width : Theme.buddyPanelWidth) - 2 * Theme.screenMargin
        // Roomier on big screens, never wider than the screen on small ones.
        const preferred = Math.max(Theme.buddyPanelWidth, Math.min(680, (desktop ? desktop.width : 0) * 0.32))
        return Math.min(preferred, Math.max(320, avail))
    }
    property real dragX: 0
    property real dragY: 0

    // Body height: defaults per kind, user-resizable with the bottom grip.
    property real bodyH: 356
    readonly property var pendingApproval: BuddyAgent ? BuddyAgent.pendingApproval : ({})
    readonly property bool awaitingApproval: pendingApproval && pendingApproval.id !== undefined
    readonly property var pendingQuestion: BuddyAgent ? BuddyAgent.pendingQuestion : ({})
    readonly property bool awaitingQuestion: pendingQuestion && pendingQuestion.id !== undefined
    // Settings > Buddy > Show thinking. Off hides the work (steps, the draft as
    // it is written, the model's reasoning); approvals and questions still show.
    readonly property bool showThinking: BuddyAgent ? BuddyAgent.showThinking : true
    readonly property string liveText: (BuddyAgent && showThinking && BuddyAgent.streaming)
                                       ? String(BuddyAgent.streamText).replace(/\*\*|`/g, "") : ""
    readonly property bool hasLive: liveText.length > 0 && phase === "loading"
    // Room the loading view gives up for approval cards or live text.
    readonly property bool compactLoading: awaitingApproval || awaitingQuestion || hasLive
    property bool userResized: false
    readonly property bool interacting: dragHit.pressed || resizeHit.pressed

    // Streaming typewriter state for AI chat responses
    property string fullResponse: payload && payload.response ? String(payload.response) : ""
    property int revealedChars: 0
    property bool isStreaming: false
    property bool instantReveal: false   // set by Buddy when the reply was streamed live
    property bool responseLoved: false
    property bool copiedFeedback: false
    property int thinkStep: 0

    readonly property var toolSteps: (BuddyAgent && showThinking) ? BuddyAgent.toolSteps : []
    readonly property string activeTool: (BuddyAgent && showThinking) ? BuddyAgent.activeTool : ""
    readonly property string activeToolBadge: (BuddyAgent && showThinking) ? BuddyAgent.activeToolBadge : ""
    readonly property string activeToolSummary: (BuddyAgent && showThinking) ? BuddyAgent.activeToolSummary : ""
    readonly property var responseMeta: (BuddyAgent) ? BuddyAgent.responseMeta : ({})
    readonly property var agentTodos: (BuddyAgent) ? BuddyAgent.agentTodos : []
    readonly property var citations: (BuddyAgent) ? BuddyAgent.citations : []
    // Screenshots Buddy put into this reply (share_screenshot).
    readonly property var attachments: (BuddyAgent) ? BuddyAgent.attachments : []
    readonly property bool hasTools: toolSteps && toolSteps.length > 0
    readonly property bool hasAdvisor: showThinking && !!(responseMeta && responseMeta.advisorUsed && String(responseMeta.advisorInsight || "").length > 0)
    readonly property bool hasAgentTodos: agentTodos && agentTodos.length > 0
    readonly property bool hasCitations: citations && citations.length > 0
    readonly property bool hasSdkTelemetry: !!(responseMeta && String(responseMeta.model || "").length > 0)
    readonly property var distinctToolIcons: {
        const seen = []
        if (!toolSteps)
            return seen
        for (let i = 0; i < toolSteps.length && seen.length < 6; ++i) {
            const ic = String(toolSteps[i].icon || "terminal")
            if (seen.indexOf(ic) < 0)
                seen.push(ic)
        }
        return seen
    }
    property bool toolsExpanded: false

    function formatDuration(ms) { return Theme.formatDuration(ms) }

    readonly property var thinkPhrases: [
        qsTr("Reading your request…"),
        qsTr("Connecting ideas…"),
        qsTr("Crafting a thoughtful answer…"),
        qsTr("Almost ready…")
    ]

    onFullResponseChanged: {
        responseLoved = false
        copiedFeedback = false
        toolsExpanded = false
        if (kind === "chat" && fullResponse.length > 0 && instantReveal) {
            // Already read live while it streamed: show it whole.
            instantReveal = false
            revealedChars = fullResponse.length
            isStreaming = false
            streamTimer.stop()
        } else if (kind === "chat" && fullResponse.length > 0) {
            revealedChars = 0
            isStreaming = true
            streamTimer.restart()
        } else {
            revealedChars = fullResponse.length
            isStreaming = false
            streamTimer.stop()
        }
    }

    onPhaseChanged: {
        if (phase === "loading") {
            thinkStep = 0
            thinkStepTimer.restart()
        } else {
            thinkStepTimer.stop()
        }
    }

    Timer {
        id: thinkStepTimer
        interval: 1350
        repeat: true
        running: panel.open && panel.phase === "loading"
        onTriggered: panel.thinkStep = (panel.thinkStep + 1) % panel.thinkPhrases.length
    }

    Timer {
        id: streamTimer
        interval: 22
        repeat: true
        running: panel.open && panel.isStreaming && panel.phase === "ready" && panel.kind === "chat"
        onTriggered: {
            const total = panel.fullResponse.length
            if (panel.revealedChars >= total) {
                panel.isStreaming = false
                stop()
                return
            }
            // Adaptive step so both short and long replies finish smoothly in ~0.8-2.0s
            const step = Math.max(3, Math.ceil(total / 68))
            let next = Math.min(total, panel.revealedChars + step)
            // Snap to next space for natural word-chunk cadence when close
            while (next < total && next - panel.revealedChars < step + 5 && panel.fullResponse.charAt(next) !== " ")
                next++
            panel.revealedChars = next
            panel.speakingTick()
            if (BuddySound && Math.random() < 0.42)
                BuddySound.play("speak")
            if (chatFlick.contentHeight > chatFlick.height)
                chatFlick.contentY = Math.max(0, chatFlick.contentHeight - chatFlick.height)
            if (panel.revealedChars >= total) {
                panel.isStreaming = false
                stop()
            }
        }
    }

    Timer {
        id: copiedTimer
        interval: 1600
        onTriggered: panel.copiedFeedback = false
    }

    // Mini avatar blink inside the response panel
    property bool miniBlink: false
    Timer {
        interval: 2900
        repeat: true
        running: panel.open && panel.visible
        onTriggered: {
            panel.miniBlink = true
            miniBlinkReset.restart()
        }
    }
    Timer {
        id: miniBlinkReset
        interval: 110
        onTriggered: panel.miniBlink = false
    }

    implicitWidth: width_
    width: width_
    height: headerH + 1 + bodyH + 1 + footerH
    x: dragX
    y: dragY - 10 * (1 - pc)
    visible: opacity > 0.005

    // Critically-damped entrance, matching the notch flyouts.
    property real p: open ? 1 : 0
    Behavior on p {
        NumberAnimation {
            duration: panel.open ? 220 : 150
            easing.type: panel.open ? Easing.OutQuint : Easing.InCubic
        }
    }
    readonly property real pc: Math.max(0, Math.min(1, p))
    opacity: Math.min(1, pc * 1.8)
    scale: 0.95 + 0.05 * pc
    transformOrigin: Item.Top

    // The reply (chat card, chart, plan...) eases in when it lands, instead of
    // popping in while the loading view fades out.
    property real readyIn: panel.phase === "ready" ? 1 : 0
    Behavior on readyIn { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }

    onKindChanged: resetBody()
    onOpenChanged: {
        if (!open) {
            userResized = false
            isStreaming = false
            streamTimer.stop()
        } else {
            resetBody()
            // Reopening a reply that was interrupted mid-cascade picks the
            // typewriter back up where it stopped instead of showing it blank.
            if (kind === "chat" && phase === "ready"
                    && revealedChars < fullResponse.length) {
                isStreaming = true
                streamTimer.restart()
            }
        }
    }

    function resetBody() {
        if (userResized)
            return
        const avail = maxBody()
        bodyH = kind === "chart" ? Math.min(avail, 344)
              : kind === "tasks" ? Math.min(avail, 284)
              : kind === "help"  ? Math.min(avail, 326)
              : Math.min(avail, 356)
    }

    function maxBody() {
        return desktop
            ? Math.max(220, desktop.height - Math.max(10, dragY) - headerH - footerH - 18)
            : 480
    }

    // Keep the card inside the desktop rect (also called by Buddy).
    function clampIntoView() {
        if (!desktop)
            return
        const lo = 10
        dragX = Math.round(Math.max(lo, Math.min(desktop.width - width_ - lo, dragX)))
        dragY = Math.round(Math.max(lo, Math.min(desktop.height - height - lo, dragY)))
    }

    Glass {
        anchors.fill: parent
        radius: 20
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ---- header -------------------------------------------------------
        Item {
            width: parent.width
            height: panel.headerH

            MouseArea {
                id: dragHit
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor
                property real gx: 0
                property real gy: 0
                property real sx: 0
                property real sy: 0
                onPressed: (mouse) => {
                    const g = panel.desktop
                              ? dragHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                              : Qt.point(mouse.x, mouse.y)
                    gx = g.x
                    gy = g.y
                    sx = panel.dragX
                    sy = panel.dragY
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const g = panel.desktop
                              ? dragHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                              : Qt.point(mouse.x, mouse.y)
                    panel.dragX = sx + (g.x - gx)
                    panel.dragY = sy + (g.y - gy)
                    panel.clampIntoView()
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 5
                width: 28
                height: 3
                radius: 1.5
                color: Theme.borderStrong
            }

            // Live animated mini-Buddy orb avatar in the header
            Rectangle {
                id: miniBuddyOrb
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26
                radius: 13
                color: Theme.orb
                border.width: 1.5
                border.color: panel.responseLoved ? Theme.love
                            : (panel.phase === "loading" || panel.isStreaming ? Theme.accent : Theme.borderStrong)

                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Repeater {
                        model: 2
                        Rectangle {
                            width: 3
                            height: panel.miniBlink ? 1 : (panel.isStreaming ? 8 : 7)
                            radius: 1.5
                            color: panel.responseLoved ? Theme.loveSoft
                                 : (panel.phase === "loading" ? Theme.accent : "#ffffff")
                            Behavior on height { NumberAnimation { duration: 80 } }
                        }
                    }
                }
            }

            Column {
                anchors.left: miniBuddyOrb.right
                anchors.leftMargin: 10
                anchors.right: chip.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    width: parent.width
                    text: panel.kind === "chat" ? qsTr("Noch Buddy") : panel.title
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: panel.kind === "chat"
                    text: panel.phase === "loading"
                          ? (panel.awaitingQuestion ? qsTr("Waiting for your answer") : panel.awaitingApproval ? qsTr("Paused on one step")
                             : panel.hasLive ? qsTr("Answering live")
                             : (BuddyAgent.activeAgent !== "buddy" && BuddyAgent.activeAgentTitle.length > 0)
                               ? qsTr("%1 is on it").arg(BuddyAgent.activeAgentTitle)
                             : panel.thinkPhrases[panel.thinkStep])
                        : (panel.isStreaming ? qsTr("Speaking…")
                        : (BuddyAgent && BuddyAgent.model ? BuddyAgent.model : qsTr("Companion")))
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    elide: Text.ElideRight
                }
            }

            // State chip: Thinking… / Speaking… / Ready / Failed.
            Rectangle {
                id: chip
                anchors.right: closeBtn.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: chipLabel.implicitWidth + 18
                height: 22
                radius: 11
                color: panel.phase === "error"
                       ? Theme.dangerSoft
                       : (panel.phase === "loading" || panel.isStreaming
                          ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                          : Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16))
                border.width: 1
                border.color: panel.phase === "error"
                              ? Theme.danger
                              : (panel.phase === "loading" || panel.isStreaming
                                 ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                                 : Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.45))

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: panel.phase === "error" ? qsTr("Failed")
                        : (panel.phase === "loading" ? (panel.awaitingApproval || panel.awaitingQuestion ? qsTr("Needs you") : panel.hasLive ? qsTr("Writing…") : qsTr("Thinking…"))
                        : (panel.isStreaming ? qsTr("Live") : qsTr("Ready")))
                    color: panel.phase === "error" ? Theme.danger
                        : (panel.phase === "loading" || panel.isStreaming ? Theme.accent : Theme.success)
                    font.family: Theme.font
                    font.pixelSize: Theme.fontXs
                    font.weight: Font.DemiBold
                }
            }

            Item {
                id: closeBtn
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24

                Rectangle {
                    anchors.fill: parent
                    radius: 7
                    color: closeHit.containsMouse ? Theme.hover : "transparent"
                }

                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 13
                    stroke: 2
                    color: closeHit.containsMouse ? Theme.ink : Theme.textFaint
                }
                MouseArea {
                    id: closeHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.dismissed()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSoft
            }
        }

        // ---- body -----------------------------------------------------------
        Item {
            width: parent.width
            height: panel.bodyH
            clip: true

            // Animated, deeply human-lovable Thinking / Loading experience
            Item {
                id: loadArea
                anchors.fill: parent
                opacity: panel.phase === "loading" ? 1 : 0
                visible: opacity > 0.01
                enabled: panel.phase === "loading"
                Behavior on opacity { NumberAnimation { duration: 160 } }

                // Show user's prompt bubble at the top while thinking
                Rectangle {
                    id: promptBox
                    visible: panel.userPrompt.length > 0
                    x: 14
                    y: 14
                    width: parent.width - 28
                    height: Math.min(68, promptPreviewText.implicitHeight + 18)
                    radius: 12
                    color: Theme.raised
                    border.width: 0
                    border.color: Theme.borderSoft

                    Text {
                        id: promptPreviewText
                        x: 12
                        y: 9
                        width: parent.width - 24
                        text: panel.userPrompt
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontBase
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                Column {
                    id: loadCol
                    // Streaming text grows, so pin it under the prompt and
                    // let the live well take what is left; otherwise centre.
                    readonly property real topY: promptBox.visible ? promptBox.y + promptBox.height + 10 : 14
                    x: (loadArea.width - width) / 2
                    y: panel.hasLive ? topY
                       : Math.max(topY, (loadArea.height - height) / 2
                                  + (panel.userPrompt.length > 0 ? (panel.compactLoading ? 24 : 18) : 0))
                    spacing: panel.compactLoading ? 8 : 14

                    // Animated expressive thinking orb (folded away while an
                    // approval card needs the room)
                    Item {
                        visible: !panel.compactLoading
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56
                        height: 56

                        Rectangle {
                            anchors.centerIn: parent
                            width: 56
                            height: 56
                            radius: 28
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.accent
                            opacity: 0.35
                            SequentialAnimation on scale {
                                running: panel.phase === "loading" && panel.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 1.22; duration: 750; easing.type: Easing.OutQuad }
                                NumberAnimation { to: 0.96; duration: 750; easing.type: Easing.InQuad }
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            radius: 22
                            color: Theme.orb
                            border.width: 1.5
                            border.color: Theme.accent

                            Row {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -3
                                spacing: 7
                                Repeater {
                                    model: 2
                                    Rectangle {
                                        width: 5
                                        height: panel.miniBlink ? 1 : 11
                                        radius: 2.5
                                        color: Theme.accent
                                        Behavior on height { NumberAnimation { duration: 80 } }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: !panel.hasLive   // the header chip already says "Writing…"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: !panel.showThinking ? qsTr("Working on it…")
                            : panel.activeToolSummary.length > 0 ? panel.activeToolSummary
                            : (panel.kind === "chart" ? qsTr("Pulling up the demo chart…")
                            : (panel.kind === "research" ? qsTr("Gathering sample notes…")
                            : (panel.kind === "tasks" ? qsTr("Sketching a plan…")
                            : panel.thinkPhrases[panel.thinkStep])))
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.fontMd
                        font.weight: Font.DemiBold
                    }

                    // Rhythmic wave bars
                    Row {
                        visible: !panel.compactLoading
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 5
                        height: 16
                        Repeater {
                            model: [0, 110, 220, 330, 440]
                            Rectangle {
                                id: waveBar
                                required property int modelData
                                anchors.verticalCenter: parent.verticalCenter
                                width: 4
                                height: 6
                                radius: 2
                                color: Theme.accent
                                SequentialAnimation on height {
                                    running: panel.phase === "loading" && panel.visible
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: waveBar.modelData }
                                    NumberAnimation { to: 15; duration: 300; easing.type: Easing.InOutSine }
                                    NumberAnimation { to: 5; duration: 300; easing.type: Easing.InOutSine }
                                }
                            }
                        }
                    }

                    // Live tool timeline while loading (last four steps)
                    Column {
                        id: loadTools
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        spacing: 3
                        visible: panel.hasTools

                        Repeater {
                            model: panel.hasTools ? panel.toolSteps.slice(panel.hasLive ? -2 : panel.awaitingApproval ? -3 : -4) : []
                            ToolStepRow {
                                required property var modelData
                                width: parent.width
                                step: modelData
                            }
                        }
                    }

                    // Live streamed answer.
                    Rectangle {
                        visible: panel.hasLive
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        height: Math.min(panelLive.implicitHeight + 24,
                                         Math.max(72, loadArea.height - loadCol.topY - 14
                                                      - (loadTools.visible ? loadTools.height + loadCol.spacing : 0)))
                        radius: Theme.radiusMd
                        color: Theme.glassFill
                        border.width: 0
                        border.color: Theme.glassBorder
                        clip: true
                        Text {
                            id: panelLive
                            x: 14
                            width: parent.width - 28
                            y: Math.min(12, parent.height - 12 - implicitHeight)
                            text: panel.liveText + " \u258F"
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.family: Theme.font
                            font.pixelSize: Theme.fontMd
                            lineHeight: 1.2
                            wrapMode: Text.Wrap
                        }
                        Rectangle {
                            visible: panelLive.y < 0
                            width: parent.width
                            height: 24
                            gradient: Gradient {
                                GradientStop { position: 0; color: Theme.surface }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                    }

                    // Human-in-the-loop approval for a paused tool call.
                    ToolApprovalCard {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        visible: panel.awaitingApproval
                        request: panel.pendingApproval
                        onDecided: (approve, always) => BuddyAgent.resolveApproval(approve, always)
                    }

                    // Buddy would rather ask than guess.
                    BuddyQuestionCard {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        maxCardHeight: Math.max(160, panel.bodyH - 60)
                        visible: panel.awaitingQuestion
                        request: panel.pendingQuestion
                        onAnswered: (answers) => BuddyAgent.answerQuestion(answers)
                        onSkipped: BuddyAgent.skipQuestion()
                    }
                }
            }

            // Error cover.
            Item {
                anchors.fill: parent
                opacity: panel.phase === "error" ? 1 : 0
                visible: opacity > 0.01
                enabled: panel.phase === "error"

                Column {
                    anchors.centerIn: parent
                    spacing: 12

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 42
                        height: 42
                        radius: 21
                        color: Theme.dangerSoft
                        border.width: 1
                        border.color: Theme.danger

                        Row {
                            anchors.centerIn: parent
                            spacing: 6
                            Repeater {
                                model: 2
                                Rectangle {
                                    width: 5
                                    height: 8
                                    radius: 2.5
                                    color: Theme.danger
                                }
                            }
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 60
                        text: panel.errorMessage
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontBase
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 104
                        height: 30
                        radius: 15
                        color: retryBtn.pressed ? Theme.press
                             : (retryBtn.containsMouse ? Theme.tileActive : Theme.tile)
                        border.width: 0
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Try again")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: retryBtn
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.retryRequested()
                        }
                    }
                }
            }

            BuddyChartCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "chart"
            }
            BuddyResearchCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "research"
            }
            BuddyTasksCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "tasks"
            }
            BuddyHelpCard {
                anchors.fill: parent
                host: panel
                visible: panel.phase === "ready" && panel.kind === "help"
            }

            // Conversational AI chat response with live streaming & reactions
            Flickable {
                id: chatFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: Math.max(height, chatCol.implicitHeight + 28)
                interactive: contentHeight > height + 1
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                visible: panel.phase === "ready" && panel.kind === "chat"
                opacity: panel.readyIn

                Column {
                    id: chatCol
                    x: 14
                    y: 14 + (1 - panel.readyIn) * 10
                    width: parent.width - 28
                    spacing: 12

                    // User prompt bubble
                    Rectangle {
                        visible: panel.userPrompt.length > 0
                        anchors.right: parent.right
                        width: Math.min(parent.width - 24, userBubbleText.implicitWidth + 24)
                        height: userBubbleText.implicitHeight + 16
                        radius: 13
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                        border.width: 0
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)

                        Text {
                            id: userBubbleText
                            x: 12
                            y: 8
                            width: Math.min(chatCol.width - 48, implicitWidth)
                            text: panel.userPrompt
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Agent execution trace: summary bar + expandable step cards
                    Column {
                        visible: panel.hasTools
                        width: parent.width
                        spacing: 6

                        ToolTraceBar {
                            width: parent.width
                            undoSource: BuddyAgent
                            steps: panel.toolSteps
                            icons: panel.distinctToolIcons
                            durationMs: panel.responseMeta ? Number(panel.responseMeta.durationMs || 0) : 0
                            expanded: panel.toolsExpanded
                            onToggled: panel.toolsExpanded = !panel.toolsExpanded
                        }

                        Column {
                            id: panelToolCol
                            width: parent.width
                            spacing: 5
                            visible: panel.toolsExpanded

                            Repeater {
                                model: panel.toolsExpanded ? panel.toolSteps : []
                                ToolStepCard {
                                    required property var modelData
                                    width: panelToolCol.width
                                    step: modelData
                                    onContinueRequested: panel.continueRequested()
                                }
                            }
                        }
                    }

                    // Anthropic Advisor Strategy Callout Card
                    Rectangle {
                        visible: panel.hasAdvisor
                        width: parent.width
                        height: pAdvisorCol.implicitHeight + 18
                        radius: 12
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                        border.width: 0
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)

                        Column {
                            id: pAdvisorCol
                            x: 12
                            y: 9
                            width: parent.width - 24
                            spacing: 4

                            Row {
                                spacing: 6
                                Glyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "advisor"
                                    size: 13
                                    stroke: 1.8
                                    color: Theme.accent
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("ANTHROPIC ADVISOR STRATEGY · %1")
                                          .arg(String(panel.responseMeta && panel.responseMeta.advisorFocus ? panel.responseMeta.advisorFocus : "GENERAL").toUpperCase())
                                    color: Theme.accent
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontXs
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.6
                                }
                            }

                            Text {
                                width: parent.width
                                text: String(panel.responseMeta && panel.responseMeta.advisorInsight ? panel.responseMeta.advisorInsight : "")
                                      .replace(/[#*`]/g, "").trim()
                                color: Theme.inkSoft
                                font.family: Theme.font
                                font.pixelSize: Theme.fontBase
                                wrapMode: Text.WordWrap
                                maximumLineCount: 4
                                elide: Text.ElideRight
                                lineHeight: 1.18
                            }
                        }
                    }

                    // Anthropic TodoWrite Live Checklist Card
                    Rectangle {
                        visible: panel.hasAgentTodos
                        width: parent.width
                        height: pTodoCol.implicitHeight + 18
                        radius: 12
                        color: Theme.raised
                        border.width: 0
                        border.color: Theme.borderSoft

                        Column {
                            id: pTodoCol
                            x: 12
                            y: 9
                            width: parent.width - 24
                            spacing: 5

                            Row {
                                spacing: 6
                                Glyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "tasks"
                                    size: 12
                                    stroke: 1.8
                                    color: Theme.accent
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("AGENT TASK CHECKLIST (%1)").arg(panel.agentTodos.length)
                                    color: Theme.textDim
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontXs
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.6
                                }
                            }

                            Repeater {
                                model: panel.agentTodos
                                Row {
                                    id: todoRow
                                    required property var modelData
                                    width: pTodoCol.width
                                    spacing: 8
                                    readonly property bool done: modelData.status === "completed"
                                    readonly property bool active: modelData.status === "in_progress"

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 15
                                        height: 15
                                        radius: 4.5
                                        color: todoRow.done ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.18)
                                             : (todoRow.active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent")
                                        border.width: 1
                                        border.color: todoRow.done ? Theme.success : (todoRow.active ? Theme.accent : Theme.borderStrong)

                                        Glyph {
                                            visible: todoRow.done || todoRow.active
                                            anchors.centerIn: parent
                                            name: todoRow.done ? "check" : "bolt"
                                            size: 9.5
                                            stroke: 2.0
                                            color: todoRow.done ? Theme.success : Theme.accent
                                        }
                                    }

                                    Text {
                                        width: parent.width - 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: String(todoRow.modelData.content || "")
                                        color: parent.done ? Theme.textDim : Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontBase
                                        font.strikeout: parent.done
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    // Buddy response bubble card
                    Rectangle {
                        width: parent.width
                        height: chatText.implicitHeight + lengthNote.h + shotsCol.h + actionRow.height + 30
                        radius: 14
                        color: Theme.raised
                        border.width: 0
                        border.color: panel.isStreaming
                                      ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                                      : Theme.borderSoft
                        Behavior on border.color { ColorAnimation { duration: 180 } }

                        TextEdit {
                            id: chatText
                            x: 14
                            y: 12
                            width: parent.width - 28
                            readOnly: true
                            selectByMouse: true
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.inverse
                            text: panel.fullResponse.length > 0
                                  ? (panel.fullResponse.slice(0, panel.revealedChars)
                                     + (panel.isStreaming ? " ▍" : ""))
                                  : qsTr("No response.")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontMd
                            wrapMode: TextEdit.WordWrap

                            MouseArea {
                                anchors.fill: parent
                                visible: panel.isStreaming
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    panel.revealedChars = panel.fullResponse.length
                                    panel.isStreaming = false
                                }
                            }
                        }

                        // Long answers: a quiet note when the reply needed
                        // "continue" requests, or was still cut off after them.
                        Text {
                            id: lengthNote
                            readonly property var meta: panel.responseMeta || {}
                            readonly property bool cut: !!meta.truncated
                            readonly property int parts: Number(meta.continuations || 0)
                            readonly property bool shown: panel.kind === "chat" && !panel.isStreaming
                                                          && panel.phase === "ready" && (cut || parts > 0)
                            readonly property real h: shown ? implicitHeight + 6 : 0
                            x: 14
                            y: chatText.y + chatText.implicitHeight + 6
                            width: parent.width - 28
                            visible: shown
                            text: cut ? qsTr("Reply hit the length limit and may be cut off. Ask me to continue.")
                                      : qsTr("Long reply, continued %n time(s)", "", parts)
                            color: cut ? Theme.warning : Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // Screenshots Buddy shared, once the words are in.
                        Column {
                            id: shotsCol
                            readonly property real h: visible ? implicitHeight + 8 : 0
                            x: 14
                            y: chatText.y + chatText.implicitHeight + lengthNote.h + 8
                            width: parent.width - 28
                            spacing: 8
                            visible: panel.kind === "chat" && panel.phase === "ready" && !panel.isStreaming
                                     && panel.attachments.length > 0
                            Repeater {
                                model: panel.attachments
                                BuddyShot {
                                    required property var modelData
                                    width: shotsCol.width
                                    source: modelData.url
                                    caption: modelData.caption || ""
                                    pixelWidth: modelData.width || 0
                                    pixelHeight: modelData.height || 0
                                }
                            }
                        }

                        // Action pills row (Copy, Regenerate, Love reaction)
                        Row {
                            id: actionRow
                            x: 12
                            y: chatText.y + chatText.implicitHeight + lengthNote.h + shotsCol.h + 10
                            height: 26
                            spacing: 6
                            opacity: panel.isStreaming ? 0.45 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }

                            // Copy button
                            Rectangle {
                                id: pCopyBtn
                                width: copyRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: copyHit.containsMouse ? Theme.tileActive : Theme.tile
                                border.width: 0
                                border.color: panel.copiedFeedback ? Theme.success : Theme.border

                                Row {
                                    id: copyRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: panel.copiedFeedback ? "check" : "clipboard"
                                        size: 12
                                        color: panel.copiedFeedback ? Theme.success : Theme.textDim
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: panel.copiedFeedback ? qsTr("Copied") : qsTr("Copy")
                                        color: panel.copiedFeedback ? Theme.success : Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.DemiBold
                                    }
                                }
                                MouseArea {
                                    id: copyHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        chatText.selectAll()
                                        chatText.copy()
                                        chatText.deselect()
                                        panel.copiedFeedback = true
                                        copiedTimer.restart()
                                        if (BuddySound)
                                            BuddySound.play("wake")
                                    }
                                }
                            }

                            // Retry button
                            Rectangle {
                                id: pRetryBtn
                                width: retryActionRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: retryActionHit.containsMouse ? Theme.tileActive : Theme.tile
                                border.width: 0
                                border.color: Theme.border

                                Row {
                                    id: retryActionRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "reset"
                                        size: 12
                                        color: Theme.textDim
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Regenerate")
                                        color: Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.DemiBold
                                    }
                                }
                                MouseArea {
                                    id: retryActionHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: panel.retryRequested()
                                }
                            }

                            // Love / Thank Buddy reaction button
                            Rectangle {
                                id: pLoveBtn
                                width: loveRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: panel.responseLoved
                                       ? Qt.rgba(1.0, 0.42, 0.62, 0.18)
                                       : (loveHit.containsMouse ? Theme.tileActive : Theme.tile)
                                border.width: 1
                                border.color: panel.responseLoved ? Theme.love : Theme.border
                                scale: loveHit.pressed ? 0.92 : 1.0
                                Behavior on scale { NumberAnimation { duration: 100 } }

                                Row {
                                    id: loveRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "heart"
                                        filled: panel.responseLoved
                                        size: 12
                                        color: panel.responseLoved ? Theme.love : Theme.textDim
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: panel.responseLoved ? qsTr("Loved!") : qsTr("Thank Buddy")
                                        color: panel.responseLoved ? Theme.love : Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.DemiBold
                                    }
                                }
                                MouseArea {
                                    id: loveHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        panel.responseLoved = true
                                        panel.loved()
                                    }
                                }
                            }

                            // Anthropic Agent SDK Telemetry Badge on the right of actionRow
                            Rectangle {
                                id: pMetaChip
                                visible: panel.hasSdkTelemetry
                                height: 24
                                // Whatever the three buttons leave; the text
                                // elides instead of running off the card.
                                readonly property real room: Math.max(64, actionRow.parent.width - 24 - pCopyBtn.width
                                                                      - pRetryBtn.width - pLoveBtn.width - 3 * actionRow.spacing)
                                width: Math.min(pSdkMetaRow.implicitWidth + 14, room)
                                radius: 12
                                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.04)
                                border.width: 0
                                border.color: Theme.borderSoft

                                Row {
                                    id: pSdkMetaRow
                                    anchors.centerIn: parent
                                    spacing: 5

                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "bolt"
                                        size: 10
                                        stroke: 1.8
                                        color: Theme.accent
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: {
                                            const m = panel.responseMeta || {}
                                            const parts = []
                                            if (Number(m.turns || 0) > 1) parts.push(m.turns + " turns")
                                            if (Number(m.handoffs || 0) > 0) parts.push(m.handoffs === 1 ? "1 handoff" : m.handoffs + " handoffs")
                                            if (Number(m.totalTokens || 0) > 0) parts.push(m.totalTokens + " tok")
                                            if (Number(m.durationMs || 0) > 0) parts.push(panel.formatDuration(m.durationMs))
                                            // Model last: it is the part that elides first.
                                            if (m.model) parts.push(String(m.model))
                                            return parts.join(" · ")
                                        }
                                        width: Math.min(implicitWidth, pMetaChip.room - 14 - 15)
                                        elide: Text.ElideRight
                                        color: Theme.textDim
                                        font.family: Theme.monoFont
                                        font.pixelSize: Theme.fontXs
                                    }
                                }
                            }
                        }
                    }

                    // Clickable Web Search & Fetch Citations Card
                    Flow {
                        visible: panel.hasCitations && !panel.isStreaming
                        width: parent.width
                        spacing: 6

                        Repeater {
                            model: panel.citations ? panel.citations.slice(0, 6) : []

                            Rectangle {
                                id: pCiteChip
                                required property var modelData
                                width: Math.min(chatCol.width, pCiteRow.implicitWidth + 16)
                                height: 24
                                radius: 12
                                color: pCiteHit.containsMouse ? Theme.hover : Theme.raised
                                border.width: 0
                                border.color: pCiteHit.containsMouse ? Theme.accent : Theme.borderSoft

                                Row {
                                    id: pCiteRow
                                    x: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5

                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "globe"
                                        size: 11
                                        stroke: 1.7
                                        color: Theme.accent
                                    }

                                    Text {
                                        width: Math.min(180, implicitWidth)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: String(pCiteChip.modelData.title || pCiteChip.modelData.url || "")
                                        color: Theme.inkSoft
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontSm
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: pCiteHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (pCiteChip.modelData.url) Qt.openUrlExternally(pCiteChip.modelData.url)
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }

        // ---- footer -----------------------------------------------------------
        Item {
            width: parent.width
            height: panel.footerH

            // Inline follow-up chat input when in AI chat mode
            Rectangle {
                visible: panel.kind === "chat"
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                radius: 17
                color: Theme.field
                border.width: 1
                border.color: followUpInput.activeFocus ? Theme.accent : Theme.fieldBorder
                Behavior on border.color { ColorAnimation { duration: 130 } }

                Glyph {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "spark"
                    size: 13
                    color: followUpInput.activeFocus ? Theme.accent : Theme.textFaint
                }

                TextInput {
                    id: followUpInput
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Follow-up message")
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    anchors.right: followSendBtn.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.inverse
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    font.weight: Font.Medium
                    clip: true
                    selectByMouse: true
                    enabled: panel.phase !== "loading"

                    Keys.onReturnPressed: (event) => {
                        const q = followUpInput.text.trim()
                        if (q.length > 0) {
                            followUpInput.text = ""
                            panel.followUpSubmitted(q)
                        }
                        event.accepted = true
                    }
                    Keys.onEnterPressed: (event) => {
                        const q = followUpInput.text.trim()
                        if (q.length > 0) {
                            followUpInput.text = ""
                            panel.followUpSubmitted(q)
                        }
                        event.accepted = true
                    }

                    Text {
                        anchors.fill: parent
                        visible: followUpInput.text.length === 0
                        verticalAlignment: TextInput.AlignVCenter
                        text: qsTr("Ask Buddy a follow-up…")
                        color: Theme.textFaint
                        font: followUpInput.font
                        elide: Text.ElideRight
                    }
                }

                Item {
                    id: followSendBtn
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property bool hasText: followUpInput.text.trim().length > 0
                    width: 28
                    height: 28
                    opacity: hasText || followSendHit.containsMouse ? 1.0 : 0.78
                    scale: followSendHit.pressed ? 0.90 : (followSendHit.containsMouse ? 1.05 : 1.0)
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }

                    // Stone send button: light thumb when there is text, quiet well otherwise.
                    Rectangle {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        radius: Theme.radiusSm + 2
                        color: followSendBtn.hasText
                               ? (followSendHit.containsMouse ? Theme.accentHover : Theme.accent)
                               : (followSendHit.containsMouse ? Theme.glassFillActive : Theme.glassFill)
                        border.width: followSendBtn.hasText ? 0 : 1
                        border.color: Theme.glassBorder
                        scale: followSendHit.pressed ? 0.94 : 1
                        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
                    }

                    Glyph {
                        anchors.centerIn: parent
                        name: "send"
                        size: 14
                        color: followSendBtn.hasText ? Theme.inverse : Theme.textDim
                    }
                    MouseArea {
                        id: followSendHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const q = followUpInput.text.trim()
                            if (q.length > 0) {
                                followUpInput.text = ""
                                panel.followUpSubmitted(q)
                            }
                        }
                    }
                }
            }

            // Standard footer for non-chat cards (chart, research, tasks, help)
            Item {
                anchors.fill: parent
                visible: panel.kind !== "chat"

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: panel.pad
                    anchors.verticalCenter: parent.verticalCenter
                    width: sampleLabel.implicitWidth + 16
                    height: 22
                    radius: 11
                    color: panel.simulated
                           ? Theme.tile
                           : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
                    border.width: 1
                    border.color: panel.simulated
                                  ? Theme.border
                                  : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

                    Text {
                        id: sampleLabel
                        anchors.centerIn: parent
                        text: panel.simulated ? qsTr("Sample content") : qsTr("Handled by Noch")
                        color: panel.simulated ? Theme.textDim : Theme.accent
                        font.family: Theme.font
                        font.pixelSize: Theme.fontXs
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: panel.pad
                    anchors.verticalCenter: parent.verticalCenter
                    width: 122
                    height: 26
                    radius: 13
                    color: newReqHit.pressed ? Theme.press
                          : (newReqHit.containsMouse ? Theme.tileActive : Theme.tile)
                    border.width: 1
                    border.color: Theme.border

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("New request")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: newReqHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.newRequestRequested()
                    }
                }
            }
        }
    }

    // Bottom grip: drag to change the body height, clamped into the desktop.
    MouseArea {
        id: resizeHit
        visible: panel.phase === "ready"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        width: Math.max(60, panel.width_ - 300)
        height: 12
        cursorShape: Qt.SizeVerCursor
        property real gy: 0
        property real startBody: 0
        onPressed: (mouse) => {
            const g = panel.desktop
                      ? resizeHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                      : Qt.point(mouse.x, mouse.y)
            gy = g.y
            startBody = panel.bodyH
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const g = panel.desktop
                      ? resizeHit.mapToItem(panel.desktop, mouse.x, mouse.y)
                      : Qt.point(mouse.x, mouse.y)
            panel.userResized = true
            panel.bodyH = Math.max(200, Math.min(panel.maxBody(), startBody + (g.y - gy)))
            panel.clampIntoView()
        }
    }
}
