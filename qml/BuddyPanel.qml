import QtQuick
import QtQuick.Shapes

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
    signal loved()
    signal speakingTick()
    signal planEdited(var items)
    signal capability(string id)


    readonly property real pad: 14
    readonly property real headerH: 48
    readonly property real footerH: kind === "chat" ? 50 : 42
    readonly property real width_: 460
    property real dragX: 0
    property real dragY: 0

    // Body height: defaults per kind, user-resizable with the bottom grip.
    property real bodyH: 356
    property bool userResized: false
    readonly property bool interacting: dragHit.pressed || resizeHit.pressed

    // Streaming typewriter state for AI chat responses
    property string fullResponse: payload && payload.response ? String(payload.response) : ""
    property int revealedChars: 0
    property bool isStreaming: false
    property bool responseLoved: false
    property bool copiedFeedback: false
    property int thinkStep: 0

    readonly property var toolSteps: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.toolSteps : []
    readonly property string activeTool: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.activeTool : ""
    readonly property string activeToolBadge: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.activeToolBadge : ""
    readonly property string activeToolSummary: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.activeToolSummary : ""
    readonly property var responseMeta: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.responseMeta : ({})
    readonly property var agentTodos: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.agentTodos : []
    readonly property var citations: (typeof buddyProvider !== "undefined" && buddyProvider) ? buddyProvider.citations : []
    readonly property bool hasTools: toolSteps && toolSteps.length > 0
    readonly property bool hasAdvisor: !!(responseMeta && responseMeta.advisorUsed && String(responseMeta.advisorInsight || "").length > 0)
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

    function formatDuration(ms) {
        const n = Number(ms || 0)
        if (n <= 0) return ""
        if (n < 1000) return n + "ms"
        return (n / 1000).toFixed(1) + "s"
    }

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
        if (kind === "chat" && fullResponse.length > 0) {
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
            if (typeof buddySfx !== "undefined" && buddySfx && Math.random() < 0.42)
                buddySfx.play("speak")
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
                border.color: panel.responseLoved ? "#ff6b9d"
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
                            color: panel.responseLoved ? "#ffd1e3"
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
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: panel.kind === "chat"
                    text: panel.phase === "loading" ? panel.thinkPhrases[panel.thinkStep]
                        : (panel.isStreaming ? qsTr("Speaking…")
                        : (buddyProvider && buddyProvider.model ? buddyProvider.model : qsTr("Companion")))
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: 11
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
                        : (panel.phase === "loading" ? qsTr("Thinking…")
                        : (panel.isStreaming ? qsTr("Live") : qsTr("Ready")))
                    color: panel.phase === "error" ? Theme.danger
                        : (panel.phase === "loading" || panel.isStreaming ? Theme.accent : Theme.success)
                    font.family: Theme.font
                    font.pixelSize: 10
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
                anchors.fill: parent
                opacity: panel.phase === "loading" ? 1 : 0
                visible: opacity > 0.01
                enabled: panel.phase === "loading"
                Behavior on opacity { NumberAnimation { duration: 160 } }

                // Show user's prompt bubble at the top while thinking
                Rectangle {
                    visible: panel.userPrompt.length > 0
                    x: 14
                    y: 14
                    width: parent.width - 28
                    height: Math.min(68, promptPreviewText.implicitHeight + 18)
                    radius: 12
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.borderSoft

                    Text {
                        id: promptPreviewText
                        x: 12
                        y: 9
                        width: parent.width - 24
                        text: panel.userPrompt
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                Column {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: panel.userPrompt.length > 0 ? 18 : 0
                    spacing: 14

                    // Animated expressive thinking orb
                    Item {
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
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: panel.activeToolSummary.length > 0 ? panel.activeToolSummary
                            : (panel.kind === "chart" ? qsTr("Pulling up the demo chart…")
                            : (panel.kind === "research" ? qsTr("Gathering sample notes…")
                            : (panel.kind === "tasks" ? qsTr("Sketching a plan…")
                            : panel.thinkPhrases[panel.thinkStep])))
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    // Rhythmic wave bars
                    Row {
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

                    // Live tool execution timeline while loading
                    Column {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: panel.width_ - 48
                        spacing: 5
                        visible: panel.hasTools

                        Repeater {
                            model: panel.hasTools ? panel.toolSteps.slice(-4) : []

                            Rectangle {
                                required property var modelData
                                readonly property bool isRunning: modelData.status === "running"
                                readonly property bool isError: modelData.status === "error"
                                readonly property color statusColor: isError ? Theme.danger : (isRunning ? Theme.accent : Theme.success)
                                width: parent.width
                                height: 28
                                radius: 9
                                color: isRunning
                                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                                       : (isError ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.10) : Theme.raised)
                                border.width: 1
                                border.color: isRunning
                                              ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.38)
                                              : (isError ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35) : Theme.borderSoft)

                                Rectangle {
                                    id: pStepStatusBox
                                    x: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 17
                                    height: 17
                                    radius: 5
                                    color: Qt.rgba(parent.statusColor.r, parent.statusColor.g, parent.statusColor.b, 0.16)

                                    Glyph {
                                        anchors.centerIn: parent
                                        name: parent.parent.isRunning ? String(modelData.icon || "terminal")
                                            : (parent.parent.isError ? "close" : "check")
                                        size: 10
                                        stroke: 2.0
                                        color: parent.parent.statusColor
                                    }
                                }

                                Rectangle {
                                    id: pStepCatBadge
                                    anchors.left: pStepStatusBox.right
                                    anchors.leftMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: pStepCatText.implicitWidth + 8
                                    height: 16
                                    radius: 4
                                    color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.06)

                                    Text {
                                        id: pStepCatText
                                        anchors.centerIn: parent
                                        text: String(modelData.badge || "TOOL")
                                        color: parent.parent.isRunning ? Theme.accent : Theme.textDim
                                        font.family: Theme.font
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.4
                                    }
                                }

                                Text {
                                    anchors.left: pStepCatBadge.right
                                    anchors.leftMargin: 7
                                    anchors.right: pStepBadge.left
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: String(modelData.title || "")
                                    color: parent.isRunning ? Theme.ink : Theme.inkSoft
                                    font.family: Theme.font
                                    font.pixelSize: 12
                                    font.weight: parent.isRunning ? Font.DemiBold : Font.Medium
                                    elide: Text.ElideRight
                                }

                                Text {
                                    id: pStepBadge
                                    anchors.right: parent.right
                                    anchors.rightMargin: 9
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: parent.isRunning ? qsTr("LIVE")
                                        : (parent.isError ? qsTr("ERR")
                                        : (panel.formatDuration(modelData.durationMs) || qsTr("DONE")))
                                    color: parent.isRunning ? Theme.accent
                                         : (parent.isError ? Theme.danger : Theme.textFaint)
                                    font.family: "Consolas"
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                }
                            }
                        }
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
                        font.pixelSize: 12
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
                        border.width: 1
                        border.color: Theme.border

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Try again")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: 12
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

                Column {
                    id: chatCol
                    x: 14
                    y: 14
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
                        border.width: 1
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)

                        Text {
                            id: userBubbleText
                            x: 12
                            y: 8
                            width: Math.min(chatCol.width - 48, implicitWidth)
                            text: panel.userPrompt
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Redesigned Agent SDK Execution Trace Card
                    Rectangle {
                        visible: panel.hasTools
                        width: parent.width
                        height: 36 + (panel.toolsExpanded ? panelToolCol.implicitHeight + 16 : 0)
                        radius: 12
                        color: Theme.raised
                        border.width: 1
                        border.color: panel.toolsExpanded
                                      ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                                      : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                        clip: true
                        Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                        Item {
                            width: parent.width
                            height: 36

                            Row {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                // Stacked distinct tool icons
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: -4
                                    Repeater {
                                        model: panel.distinctToolIcons
                                        Rectangle {
                                            required property var modelData
                                            width: 20
                                            height: 20
                                            radius: 6
                                            color: Theme.tile
                                            border.width: 1
                                            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.42)
                                            Glyph {
                                                anchors.centerIn: parent
                                                name: String(modelData)
                                                size: 11
                                                stroke: 1.75
                                                color: Theme.accent
                                            }
                                        }
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Anthropic Agent SDK · %n step(s)", "", panel.toolSteps.length)
                                    color: Theme.ink
                                    font.family: Theme.font
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }

                                Rectangle {
                                    visible: panel.responseMeta && Number(panel.responseMeta.durationMs || 0) > 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: pDurChipText.implicitWidth + 10
                                    height: 18
                                    radius: 6
                                    color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.06)

                                    Text {
                                        id: pDurChipText
                                        anchors.centerIn: parent
                                        text: panel.formatDuration(panel.responseMeta ? panel.responseMeta.durationMs : 0)
                                        color: Theme.textDim
                                        font.family: "Consolas"
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }

                            Rectangle {
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: pTraceBtnLabel.implicitWidth + 14
                                height: 22
                                radius: 7
                                color: panel.toolsExpanded
                                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                       : Theme.tile

                                Text {
                                    id: pTraceBtnLabel
                                    anchors.centerIn: parent
                                    text: panel.toolsExpanded ? qsTr("Hide trace ▴") : qsTr("Inspect trace ▾")
                                    color: Theme.accent
                                    font.family: Theme.font
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.toolsExpanded = !panel.toolsExpanded
                            }
                        }

                        Column {
                            id: panelToolCol
                            x: 10
                            y: 38
                            width: parent.width - 20
                            spacing: 7
                            visible: panel.toolsExpanded

                            Repeater {
                                model: panel.toolSteps

                                Rectangle {
                                    required property var modelData
                                    readonly property bool isError: modelData.status === "error"
                                    readonly property color stepAccent: isError ? Theme.danger : Theme.success
                                    width: panelToolCol.width
                                    height: pInnerCol.implicitHeight + 14
                                    radius: 9
                                    color: Theme.tile
                                    border.width: 1
                                    border.color: isError
                                                  ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35)
                                                  : Theme.borderSoft

                                    Rectangle {
                                        x: 0
                                        y: 7
                                        width: 3
                                        height: parent.height - 14
                                        radius: 1.5
                                        color: parent.stepAccent
                                        opacity: 0.8
                                    }

                                    Column {
                                        id: pInnerCol
                                        x: 11
                                        y: 7
                                        width: parent.width - 20
                                        spacing: 4

                                        Item {
                                            width: parent.width
                                            height: 20

                                            Rectangle {
                                                id: pStepIconBox
                                                x: 0
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 18
                                                height: 18
                                                radius: 5
                                                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)

                                                Glyph {
                                                    anchors.centerIn: parent
                                                    name: String(modelData.icon || "terminal")
                                                    size: 10.5
                                                    stroke: 1.75
                                                    color: Theme.accent
                                                }
                                            }

                                            Rectangle {
                                                id: pCatBadge
                                                anchors.left: pStepIconBox.right
                                                anchors.leftMargin: 6
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: pCatBadgeText.implicitWidth + 10
                                                height: 16
                                                radius: 4
                                                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.07)

                                                Text {
                                                    id: pCatBadgeText
                                                    anchors.centerIn: parent
                                                    text: String(modelData.badge || "TOOL")
                                                    color: Theme.textDim
                                                    font.family: Theme.font
                                                    font.pixelSize: 9
                                                    font.weight: Font.Bold
                                                    font.letterSpacing: 0.4
                                                }
                                            }

                                            Text {
                                                anchors.left: pCatBadge.right
                                                anchors.leftMargin: 7
                                                anchors.right: pStepMetaRow.left
                                                anchors.rightMargin: 6
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: String(modelData.title || "")
                                                color: Theme.ink
                                                font.family: Theme.font
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }

                                            Row {
                                                id: pStepMetaRow
                                                anchors.right: parent.right
                                                anchors.verticalCenter: parent.verticalCenter
                                                spacing: 6

                                                Text {
                                                    visible: Number(modelData.durationMs || 0) > 0
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: panel.formatDuration(modelData.durationMs)
                                                    color: Theme.textFaint
                                                    font.family: "Consolas"
                                                    font.pixelSize: 10
                                                }

                                                Rectangle {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    width: 15
                                                    height: 15
                                                    radius: 7.5
                                                    color: Qt.rgba(pInnerCol.parent.stepAccent.r,
                                                                   pInnerCol.parent.stepAccent.g,
                                                                   pInnerCol.parent.stepAccent.b, 0.16)
                                                    Glyph {
                                                        anchors.centerIn: parent
                                                        name: pInnerCol.parent.isError ? "close" : "check"
                                                        size: 9
                                                        stroke: 2.2
                                                        color: pInnerCol.parent.stepAccent
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            width: parent.width
                                            visible: String(modelData.detail || "").length > 0
                                            text: String(modelData.detail || "")
                                            color: Theme.accent
                                            font.family: "Consolas"
                                            font.pixelSize: 11
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            width: parent.width
                                            height: pOutConsole.implicitHeight + 10
                                            radius: 6
                                            color: Theme.field
                                            border.width: 1
                                            border.color: Theme.borderSoft
                                            visible: String(modelData.output || "").length > 0

                                            Text {
                                                id: pOutConsole
                                                x: 8
                                                y: 5
                                                width: parent.width - 16
                                                text: String(modelData.output || "")
                                                color: Theme.textDim
                                                font.family: "Consolas"
                                                font.pixelSize: 11
                                                wrapMode: Text.WrapAnywhere
                                                maximumLineCount: 6
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
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
                        border.width: 1
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
                                    font.pixelSize: 10
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
                                font.pixelSize: 12
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
                        border.width: 1
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
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.6
                                }
                            }

                            Repeater {
                                model: panel.agentTodos
                                Row {
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
                                        color: parent.done ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.18)
                                             : (parent.active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent")
                                        border.width: 1
                                        border.color: parent.done ? Theme.success : (parent.active ? Theme.accent : Theme.borderStrong)

                                        Glyph {
                                            visible: parent.parent.done || parent.parent.active
                                            anchors.centerIn: parent
                                            name: parent.parent.done ? "check" : "bolt"
                                            size: 9.5
                                            stroke: 2.0
                                            color: parent.parent.done ? Theme.success : Theme.accent
                                        }
                                    }

                                    Text {
                                        width: parent.width - 24
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: String(modelData.content || "")
                                        color: parent.done ? Theme.textDim : Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: 12
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
                        height: chatText.implicitHeight + actionRow.height + 30
                        radius: 14
                        color: Theme.raised
                        border.width: 1
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
                            font.pixelSize: 13
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

                        // Action pills row (Copy, Regenerate, Love reaction)
                        Row {
                            id: actionRow
                            x: 12
                            y: chatText.y + chatText.implicitHeight + 10
                            height: 26
                            spacing: 6
                            opacity: panel.isStreaming ? 0.45 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }

                            // Copy button
                            Rectangle {
                                width: copyRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: copyHit.containsMouse ? Theme.tileActive : Theme.tile
                                border.width: 1
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
                                        font.pixelSize: 11
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
                                        if (typeof buddySfx !== "undefined" && buddySfx)
                                            buddySfx.play("wake")
                                    }
                                }
                            }

                            // Retry button
                            Rectangle {
                                width: retryActionRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: retryActionHit.containsMouse ? Theme.tileActive : Theme.tile
                                border.width: 1
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
                                        font.pixelSize: 11
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
                                width: loveRow.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: panel.responseLoved
                                       ? Qt.rgba(1.0, 0.42, 0.62, 0.18)
                                       : (loveHit.containsMouse ? Theme.tileActive : Theme.tile)
                                border.width: 1
                                border.color: panel.responseLoved ? "#ff6b9d" : Theme.border
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
                                        color: panel.responseLoved ? "#ff6b9d" : Theme.textDim
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: panel.responseLoved ? qsTr("Loved!") : qsTr("Thank Buddy")
                                        color: panel.responseLoved ? "#ff6b9d" : Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: 11
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
                                visible: panel.hasSdkTelemetry
                                height: 24
                                width: pSdkMetaRow.implicitWidth + 14
                                radius: 12
                                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.04)
                                border.width: 1
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
                                            if (m.model) parts.push(String(m.model))
                                            if (Number(m.turns || 0) > 1) parts.push(m.turns + " turns")
                                            if (Number(m.totalTokens || 0) > 0) parts.push(m.totalTokens + " tok")
                                            if (Number(m.durationMs || 0) > 0) parts.push(panel.formatDuration(m.durationMs))
                                            return parts.join(" · ")
                                        }
                                        color: Theme.textDim
                                        font.family: "Consolas"
                                        font.pixelSize: 10
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
                                required property var modelData
                                width: Math.min(chatCol.width, pCiteRow.implicitWidth + 16)
                                height: 24
                                radius: 12
                                color: pCiteHit.containsMouse ? Theme.hover : Theme.raised
                                border.width: 1
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
                                        text: String(modelData.title || modelData.url || "")
                                        color: Theme.inkSoft
                                        font.family: Theme.font
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: pCiteHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (modelData.url) Qt.openUrlExternally(modelData.url)
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
                    anchors.left: parent.left
                    anchors.leftMargin: 32
                    anchors.right: followSendBtn.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.inverse
                    font.family: Theme.font
                    font.pixelSize: 12
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

                    function rrBox(x0, y0, x1, y1, r) {
                        const cr = Math.max(0.5, Math.min(r, (x1 - x0) * 0.5, (y1 - y0) * 0.5))
                        return "M " + (x0 + cr) + " " + y0
                             + " L " + (x1 - cr) + " " + y0
                             + " A " + cr + " " + cr + " 0 0 1 " + x1 + " " + (y0 + cr)
                             + " L " + x1 + " " + (y1 - cr)
                             + " A " + cr + " " + cr + " 0 0 1 " + (x1 - cr) + " " + y1
                             + " L " + (x0 + cr) + " " + y1
                             + " A " + cr + " " + cr + " 0 0 1 " + x0 + " " + (y1 - cr)
                             + " L " + x0 + " " + (y0 + cr)
                             + " A " + cr + " " + cr + " 0 0 1 " + (x0 + cr) + " " + y0 + " Z"
                    }
                    function rrRing(halfSize, r, thick) {
                        const cx = width * 0.5, cy = height * 0.5
                        const ro = halfSize + thick * 0.5
                        const ri = Math.max(1, halfSize - thick * 0.5)
                        return rrBox(cx - ro, cy - ro, cx + ro, cy + ro, r + thick * 0.5) + " "
                             + rrBox(cx - ri, cy - ri, cx + ri, cy + ri, Math.max(1, r - thick * 0.5))
                    }

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        opacity: followSendBtn.hasText || followSendHit.containsMouse ? 0.28 : 0.16
                        ShapePath {
                            strokeColor: "transparent"
                            strokeWidth: -1
                            fillRule: ShapePath.OddEvenFill
                            fillGradient: ConicalGradient {
                                centerX: followSendBtn.width * 0.5
                                centerY: followSendBtn.height * 0.5
                                angle: 0
                                GradientStop { position: 0.00; color: "#d875ff" }
                                GradientStop { position: 0.28; color: "#936aff" }
                                GradientStop { position: 0.44; color: "#6662ff" }
                                GradientStop { position: 0.78; color: "#df80ff" }
                                GradientStop { position: 0.90; color: "#f2f0ff" }
                                GradientStop { position: 1.00; color: "#d875ff" }
                            }
                            PathSvg { path: followSendBtn.rrRing(11.5, 7.5, 3.8) }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 23
                        height: 23
                        radius: 7.5
                        color: followSendHit.pressed ? "#121218" : (followSendHit.containsMouse ? "#1e1e28" : "#17171f")
                        antialiasing: true
                    }

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: "transparent"
                            strokeWidth: -1
                            fillRule: ShapePath.OddEvenFill
                            fillGradient: ConicalGradient {
                                centerX: followSendBtn.width * 0.5
                                centerY: followSendBtn.height * 0.5
                                angle: 0
                                GradientStop { position: 0.00; color: "#d875ff" }
                                GradientStop { position: 0.14; color: "#c96eff" }
                                GradientStop { position: 0.28; color: "#936aff" }
                                GradientStop { position: 0.44; color: "#6662ff" }
                                GradientStop { position: 0.64; color: "#b56bff" }
                                GradientStop { position: 0.78; color: "#df80ff" }
                                GradientStop { position: 0.90; color: "#f2f0ff" }
                                GradientStop { position: 1.00; color: "#d875ff" }
                            }
                            PathSvg { path: followSendBtn.rrRing(11.5, 7.5, 1.5) }
                        }
                    }

                    Glyph {
                        anchors.centerIn: parent
                        name: "send"
                        size: 12
                        stroke: 1.85
                        color: followSendBtn.hasText || followSendHit.containsMouse ? "#df96ff" : "#ba82ff"
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
                        font.pixelSize: 10
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
                        font.pixelSize: 11
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
