pragma ComponentBehavior: Bound
import QtQuick

// The buddy's thought bubble. It pops out of the orb as a trail of little
// thought dots followed by the bubble itself, then morphs through every step
// of a request as one continuous surface:
//
//   input     a text field ("What should I help with?")
//   thinking  a compact bubble with slow, breathing dots
//   answer    grows to fit the reply: words cascade in one by one, or the
//             rich card (chart, research, tasks, help) fades up inside it
//   error     a gentle note with Try again
//   history   past questions; pick one to reopen its answer
//
// Everything is laid out in the buddy's own coordinates: this item sits at
// the buddy's origin with zero size and places its parts around the orb.
Item {
    id: tb

    // ---- wiring from Buddy ------------------------------------------------
    property real s: 1
    property real orbSize: 52
    property bool flipped: false          // bubble sits on the buddy's left
    property bool centered: false         // bubble sits centered on the buddy's head
    property bool above: true             // true = above buddy (head or +45°), false = below (-45°)
    property rect bounds: Qt.rect(-100000, -100000, 200000, 200000) // usable area
    property real mo: 1                   // 1 = full motion, < 1 = reduced

    // ---- content ----------------------------------------------------------
    property string phase: "hidden"       // hidden | input | thinking | answer | error
    property string kind: "chat"          // chat | note | chart | research | tasks | help
    property string title: ""
    property var payload: null
    property bool simulated: true
    property string errorMessage: ""
    property string noteText: ""
    property string noteIcon: "check"
    property var history: []              // [{ q, kind, title, a, t }], newest first

    // Live Anthropic Agent SDK tool & response telemetry state
    // Settings > Buddy > Show thinking. Off hides the work (steps, the draft as
    // it is written, the model's reasoning); approvals and questions still show.
    readonly property bool showThinking: BuddyAgent ? BuddyAgent.showThinking : true
    readonly property var toolSteps: (BuddyAgent && showThinking) ? BuddyAgent.toolSteps : []
    readonly property string activeTool: (BuddyAgent && showThinking) ? BuddyAgent.activeTool : ""
    readonly property string activeToolBadge: (BuddyAgent && showThinking) ? BuddyAgent.activeToolBadge : ""
    readonly property string activeToolSummary: (BuddyAgent && showThinking) ? BuddyAgent.activeToolSummary : ""
    readonly property var responseMeta: (BuddyAgent) ? BuddyAgent.responseMeta : ({})
    readonly property var agentTodos: (BuddyAgent) ? BuddyAgent.agentTodos : []
    readonly property var citations: (BuddyAgent) ? BuddyAgent.citations : []
    // Screenshots Buddy put into this reply (share_screenshot).
    readonly property var attachments: (BuddyAgent) ? BuddyAgent.attachments : []
    readonly property bool hasShots: attachments && attachments.length > 0
    // A screenshot arrives under the text: scroll it into view.
    onHasShotsChanged: if (hasShots) scrollSoon.restart()
    // After the card has grown to fit, so the overflow it measures is the real one.
    Timer { id: scrollSoon; interval: 450; onTriggered: tb.scrollToEnd() }
    function scrollToEnd() {
        const to = Math.max(0, chatFlick.contentHeight - chatFlick.height)
        if (to > chatFlick.contentY) {
            scrollEnd.to = to
            scrollEnd.restart()
        }
    }
    NumberAnimation {
        id: scrollEnd
        target: chatFlick
        property: "contentY"
        duration: 360
        easing.type: Easing.OutCubic
    }
    readonly property var pendingApproval: (BuddyAgent) ? BuddyAgent.pendingApproval : ({})
    readonly property bool awaitingApproval: pendingApproval && pendingApproval.id !== undefined
    // Buddy is asking the user a multiple-choice question (ask_user_question).
    readonly property var pendingQuestion: (BuddyAgent) ? BuddyAgent.pendingQuestion : ({})
    readonly property bool awaitingQuestion: pendingQuestion && pendingQuestion.id !== undefined
    // Specialist currently in charge (handoffs), "" for Buddy itself.
    readonly property string specialist: (BuddyAgent
                                          && BuddyAgent.activeAgent !== "buddy")
                                         ? String(BuddyAgent.activeAgentTitle).replace(" specialist", "") : ""
    // Live streamed answer text while the model is still writing.
    readonly property string liveText: (BuddyAgent && showThinking && BuddyAgent.streaming)
                                       ? String(BuddyAgent.streamText).replace(/\*\*|`/g, "") : ""
    readonly property bool hasLive: liveText.length > 0 && phase === "thinking"
    readonly property real liveH: hasLive ? Math.min(liveBody.implicitHeight, 168) + 22 : 0
    readonly property bool hasTools: toolSteps && toolSteps.length > 0
    readonly property bool hasAdvisor: showThinking && !!(responseMeta && responseMeta.advisorUsed && String(responseMeta.advisorInsight || "").length > 0)
    readonly property bool hasAgentTodos: agentTodos && agentTodos.length > 0
    readonly property bool hasCitations: citations && citations.length > 0
    readonly property bool hasSdkTelemetry: !!(responseMeta && String(responseMeta.model || "").length > 0)
    readonly property var distinctToolIcons: {
        const seen = []
        if (!toolSteps)
            return seen
        for (let i = 0; i < toolSteps.length && seen.length < 5; ++i) {
            const ic = String(toolSteps[i].icon || "terminal")
            if (seen.indexOf(ic) < 0)
                seen.push(ic)
        }
        return seen
    }
    property bool toolsExpanded: false

    function toolIcon(name) {
        if (name === "bash") return "terminal"
        if (name === "computer") return "computer"
        if (name === "str_replace_editor") return "doc"
        if (name === "glob_grep") return "code-search"
        if (name === "memory") return "memory"
        if (name === "web_search") return "globe"
        if (name === "web_fetch") return "web-fetch"
        if (name === "tool_search") return "tool-search"
        if (name === "advisor") return "advisor"
        if (name === "todo_write") return "tasks"
        return "spark"
    }

    function formatDuration(ms) { return Theme.formatDuration(ms) }

    property bool closing: false
    readonly property string fullResponse: payload && payload.response ? String(payload.response) : ""
    readonly property bool richKind: kind === "chart" || kind === "research" || kind === "tasks" || kind === "help"
    readonly property bool revealing: phase === "answer" && kind === "chat" && revealed < tokens.length
    // How much of the answer the reader has actually seen (1 = all of it).
    // Buddy keeps this so an interrupted answer can be resumed where it
    // stopped rather than starting over from the first word.
    readonly property real progress: phase === "answer" && kind === "chat" && tokens.length > 0
                                     ? Math.max(0, Math.min(1, revealed / tokens.length)) : 1.0
    // Set by Buddy just before showAnswer() to resume at a fraction of the text.
    property real resumeFrom: 0
    readonly property real side: flipped ? -1 : 1
    readonly property real vDir: above ? -1 : 1

    signal submitted(string text)
    signal dismissed()                    // the user closed it (x, Esc, note timed out)
    signal retryRequested()
    signal typed(real caretFrac)
    signal loved()
    signal speakingTick(real progress)
    signal revealFinished()
    signal expandRequested()
    signal planEdited(var items)
    signal capability(string id)
    signal historyPicked(var entry)
    signal historyCleared()
    signal historyOpened()

    readonly property color buddySurface: Qt.darker(Theme.surface, 1.22)
    readonly property color buddyField: Qt.darker(Theme.field, 1.22)
    readonly property color buddyRaised: Qt.darker(Theme.raised, 1.12)
    FontMetrics { id: bodyMetrics; font.family: Theme.font; font.pixelSize: Theme.fontMd }
    FontMetrics { id: codeMetrics; font.family: Theme.monoFont; font.pixelSize: Theme.fontBase }

    visible: bp > 0.001 || d1 > 0.001

    // ---- public API (called by Buddy) ---------------------------------------
    function openInput() {
        noteTimer.stop()
        present("input")
        Qt.callLater(focusInput)
    }
    // Dictated words go where the caret is, with a space between them and what was typed.
    function insertText(t) {
        const add = String(t || "").trim()
        if (add.length === 0)
            return
        const before = field.text.slice(0, field.cursorPosition)
        const lead = before.length > 0 && !/\s$/.test(before) ? " " : ""
        field.insert(field.cursorPosition, lead + add)
        field.forceActiveFocus()
        field.deselect()
    }
    function focusInput() {
        field.forceActiveFocus()
        field.selectAll()
    }
    function think() {
        noteTimer.stop()
        toolsExpanded = false
        present("thinking")
    }
    function showAnswer(k, ttl, data, sim) {
        noteTimer.stop()
        toolsExpanded = false
        kind = k
        title = ttl
        simulated = sim
        payload = data
        if (k === "chat") {
            tokens = tokenize(fullResponse)
            // Resume where the reader stopped if this answer was interrupted;
            // a brand-new answer starts from the first word.
            const from = Math.max(0, Math.min(1, Number(resumeFrom) || 0))
            revealed = Math.min(tokens.length, Math.round(tokens.length * from))
            resumeFrom = 0
            finishedSent = revealed >= tokens.length
            chatFlick.contentY = 0
            growH = 0
            // Once the words are laid out, open at the size of the first line.
            Qt.callLater(function() { tb.trackGrowth() })
            if (revealed > 0)
                Qt.callLater(function() { tb.followReveal() })
        }
        field.text = ""
        followField.input.text = ""
        cardAsk.input.text = ""
        present("answer")
        entrance.restart()
        arrivalPulse.restart()
        // Rich cards are ready at once; chat waits for its cascade to finish.
        if (richKind)
            Qt.callLater(function() { cardAsk.input.forceActiveFocus() })
    }
    // Marks the answer as fully read (used when it moves to the result panel).
    function revealAll() {
        revealed = tokens.length
        finishedSent = true
    }
    function showNote(text, icon) {
        kind = "note"
        noteText = text
        noteIcon = icon || "check"
        field.text = ""
        present("answer")
        entrance.restart()
        noteTimer.restart()
    }
    function showError(message) {
        noteTimer.stop()
        errorMessage = message
        present("error")
        shake.restart()
    }
    function close() {
        noteTimer.stop()
        if (!visible || closing)
            return
        closing = true
        popIn.stop()
        popOut.restart()
    }
    function showHistory() {
        noteTimer.stop()
        clearArmed = false
        present("history")
        histList.currentIndex = -1
        histList.positionViewAtBeginning()
        historyIn.restart()
        historyOpened()
        Qt.callLater(function() { histList.forceActiveFocus() })
    }
    function backToInput() {
        present("input")
        Qt.callLater(focusInput)
    }
    function pickHistory(i) {
        if (i >= 0 && i < history.length)
            historyPicked(history[i])
    }

    // "just now", "5 min ago", "3 h ago", "Yesterday", "Mon", "12 Oct"
    function timeAgo(ms) {
        const d = Date.now() - Number(ms || 0)
        if (d < 60000)
            return qsTr("just now")
        if (d < 3600000)
            return qsTr("%1 min ago").arg(Math.floor(d / 60000))
        const then = new Date(Number(ms))
        const today = new Date()
        today.setHours(0, 0, 0, 0)
        if (then >= today)
            return qsTr("%1 h ago").arg(Math.floor(d / 3600000))
        const days = Math.floor((today - then) / 86400000) + 1
        if (days === 1)
            return qsTr("Yesterday")
        if (days < 7)
            return then.toLocaleDateString(Qt.locale(), "ddd")
        return then.toLocaleDateString(Qt.locale(), "d MMM")
    }
    function kindIcon(k) {
        return k === "chart" ? "chart" : k === "research" ? "doc"
             : k === "tasks" ? "tasks" : k === "help" ? "list" : "spark"
    }

    // Shows `ph`, popping the bubble out of the orb first when it is hidden
    // (or on its way back in); when it is already up, it simply morphs.
    function present(ph) {
        if (!visible || closing) {
            popOut.stop()
            closing = false
            if (bp < 0.01) {
                // Fresh pop: jump straight to the new shape, no morph from
                // whatever size the last session ended at.
                morphing = false
                phase = ph
                morphing = true
            } else {
                phase = ph
            }
            popIn.restart()
        } else {
            phase = ph
        }
        refreshMask(true)
    }

    // ---- sizes ----------------------------------------------------------------
    readonly property real rowH: 46
    readonly property real pad: 14
    // Widths follow the screen: roomy on a big monitor, never wider than the
    // screen on a small one (every pane lays itself out from these).
    readonly property real screenW: Math.max(320, bounds.width)
    function fitW(frac, lo, hi) {
        return Math.round(Math.min(screenW - 16, Math.max(lo, Math.min(hi, screenW * frac))))
    }
    readonly property real inputW: fitW(0.26, 360, 540)
    readonly property real chatW: fitW(0.30, 420, 600)
    readonly property real cardW: fitW(0.32, 440, 620)
    readonly property real errW: fitW(0.20, 300, 380)
    readonly property real footH: 32
    readonly property real maxH: Math.max(150, bounds.height - 16)

    readonly property real toolDrawerH: hasTools && toolsExpanded
        ? Math.min(Math.max(120, maxH * 0.42), toolDrawerCol.implicitHeight)
        : 0
    readonly property real chatHeaderH: hasTools ? (36 + (toolsExpanded ? toolDrawerH + 6 : 0)) : 0
    // The text area is capped (it scrolls past that); a shared screenshot gets more room.
    readonly property real chatCap: Math.min(hasShots ? 480 : 310, maxH - 64 - chatHeaderH)
    readonly property real chatFullH: Math.max(22, Math.min(chatBodyCol.implicitHeight, chatCap))
    // While the words arrive the bubble grows with them, one line at a time,
    // instead of opening at its final size around empty space. growH only
    // changes when a new line starts, so the click-through mask is not rebuilt
    // for every word; the morph spring smooths the steps.
    property real growH: 0
    readonly property real chatContentH: revealing ? Math.min(chatFullH, Math.max(22, growH)) : chatFullH
    function trackGrowth() {
        if (!revealing)
            return
        // Make room for the next batch of words just before they appear.
        const ahead = Math.min(tokens.length - 1, revealed + Math.max(1, revealStep))
        const item = words.itemAt(Math.max(0, ahead))
        if (!item)
            return
        const need = Math.ceil(flow.y + item.y + item.height + 4)
        if (need > growH)
            growH = need
    }
    readonly property real cardBodyH: Math.min(maxH - 48,
                                               kind === "chart" ? 330
                                             : kind === "research" ? 336
                                             : kind === "tasks" ? 290 : 312)
    readonly property real thinkW: (awaitingApproval || awaitingQuestion || hasLive || (hasTools && specialist.length > 0)) ? chatW
                                 : (hasTools || activeToolSummary.length > 0)
                                 ? histW
                                 : (longThink ? 84 + thinkLabel.implicitWidth + 16 : 84)
    readonly property real thinkH: (hasTools
                                 ? (rowH + Math.min(toolSteps.length, 4) * 31 + 10)
                                 : rowH)
                                 + (awaitingApproval ? approvalCard.implicitHeight + 10 : 0)
                                 + (awaitingQuestion ? questionCard.implicitHeight + 10 : 0)
                                 + (hasLive ? liveH + 6 : 0)
    readonly property real noteW: 46 + noteLabel.implicitWidth + 18
    readonly property real errorH: 16 + errText.implicitHeight + 12 + 28 + 14
    readonly property real histW: fitW(0.27, 400, 540)
    readonly property real histRowH: 54
    readonly property real histH: 46 + (history.length > 0
                                        ? Math.min(history.length, 6) * histRowH + 8
                                        : 74)

    readonly property real rawW: phase === "thinking" ? thinkW
                               : phase === "history" ? histW
                               : phase === "error" ? errW
                               : phase === "answer" ? (kind === "chat" ? chatW
                                                     : kind === "note" ? noteW : cardW)
                               : inputW
    readonly property real rawH: phase === "thinking" ? thinkH
                               : phase === "history" ? histH
                               : phase === "error" ? errorH
                               : phase === "answer" ? (kind === "chat" ? 12 + chatHeaderH + chatContentH
                                                                          + (revealing ? 8 : 10 + footH + 8)
                                                     : kind === "note" ? rowH
                                                     : cardBodyH + 48)
                               : rowH
    readonly property real targetW: Math.min(rawW, bounds.width)
    readonly property real targetH: Math.min(rawH, maxH)

    // Size morphs between phases on a soft spring (critically damped when the
    // system asks for reduced motion).
    property bool morphing: true
    property real w: targetW
    property real h: targetH
    // Playful while a bubble pops up or takes a question; calmer once an answer
    // is arriving, so the card does not wobble under the words as they appear.
    readonly property real springDamping: tb.mo < 1 ? 0.95 : (tb.phase === "answer" ? 0.66 : 0.38)
    Behavior on w {
        enabled: tb.morphing
        SpringAnimation { spring: 3.4; damping: tb.springDamping; epsilon: 0.2 }
    }
    Behavior on h {
        enabled: tb.morphing
        SpringAnimation { spring: 3.4; damping: tb.springDamping + 0.02; epsilon: 0.2 }
    }

    // ---- placement --------------------------------------------------------------
    readonly property real orbR: orbSize * 0.5
    readonly property real headGap: Math.round(22 * s)
    readonly property real diagStep: Math.round(24 * s)
    readonly property real diagReach: Math.round(orbR * 0.7071 + diagStep)
    readonly property real anchorY: Math.round(orbR + vDir * diagReach)    // where the 45° trail meets the bubble
    readonly property real nearX: flipped ? Math.round(orbR - diagReach)
                                          : Math.round(orbR + diagReach)

    function placeX(ww) {
        const want = centered ? Math.round(orbR - ww * 0.5)
                              : (flipped ? nearX - ww : nearX)
        return Math.round(Math.max(bounds.x, Math.min(bounds.x + bounds.width - ww, want)))
    }
    function placeY(hh) {
        const want = centered
            ? (above ? -headGap - hh : orbSize + headGap)
            : (above ? anchorY + rowH / 2 - hh : anchorY - rowH / 2)
        return Math.round(Math.max(bounds.y, Math.min(bounds.y + bounds.height - hh, want)))
    }
    function paneX(ww) {
        return centered ? Math.round((stage.width - ww) * 0.5)
                        : (flipped ? stage.width - ww : 0)
    }
    readonly property real bx: placeX(w)
    readonly property real by: placeY(h)

    readonly property real tailStartX: centered
        ? Math.round(orbR)
        : Math.round(orbR + side * (orbR + 3 * s) * 0.7071)
    readonly property real tailStartY: centered
        ? (above ? -Math.round(2 * s) : orbSize + Math.round(2 * s))
        : Math.round(orbR + vDir * (orbR + 3 * s) * 0.7071)
    readonly property real tailEndX: centered
        ? Math.round(orbR)
        : (flipped ? bx + w : bx)
    readonly property real tailEndY: centered
        ? (above ? by + h : by)
        : (above ? by + h - Math.min(h, rowH) / 2 : by + Math.min(h, rowH) / 2)

    // ---- click-through mask ------------------------------------------------------
    // The window mask is a Win32 region, so it must never change per frame.
    // While a morph runs it holds the union of where the bubble is now and
    // where it is going, plus room for the spring's overshoot; once settled
    // it tightens again.
    property rect maskBody: Qt.rect(0, 0, 0, 0)
    readonly property rect maskTail: Qt.rect(Math.round(Math.min(tailStartX, tailEndX) - 10),
                                             Math.round(Math.min(tailStartY, tailEndY) - 10),
                                             Math.round(Math.abs(tailEndX - tailStartX) + 20),
                                             Math.round(Math.abs(tailEndY - tailStartY) + 20))
    readonly property string maskSig: !visible ? "-"
        : [maskBody.x, maskBody.y, maskBody.width, maskBody.height,
           maskTail.x, maskTail.y, maskTail.width, maskTail.height].join(":")

    readonly property string targetSig: [phase, kind, Math.round(targetW), Math.round(targetH),
                                         placeX(targetW), placeY(targetH), flipped, centered, above].join(":")
    onTargetSigChanged: refreshMask(true)

    function refreshMask(holding) {
        const tw = targetW, th = targetH
        const tx = placeX(tw), ty = placeY(th)
        let x0 = Math.min(tx, bx), y0 = Math.min(ty, by)
        let x1 = Math.max(tx + tw, bx + w), y1 = Math.max(ty + th, by + h)
        let m = 10
        if (holding) {
            // Spring overshoot room, proportional to how far the morph travels.
            m += Math.round(0.12 * Math.max(Math.abs(tw - w), Math.abs(th - h)))
            maskSettle.restart()
        }
        maskBody = Qt.rect(Math.round(x0 - m), Math.round(y0 - m),
                           Math.round(x1 - x0 + 2 * m), Math.round(y1 - y0 + 2 * m))
    }
    Timer { id: maskSettle; interval: 1100; onTriggered: tb.refreshMask(false) }

    // ---- pop in / out ----------------------------------------------------------------
    property real d1: 0     // trail dot nearest the orb
    property real d2: 0
    property real d3: 0     // trail dot nearest the bubble
    property real bp: 0     // the bubble itself

    readonly property int popEase: tb.mo < 1 ? Easing.OutCubic : Easing.OutBack

    ParallelAnimation {
        id: popIn
        NumberAnimation { target: tb; property: "d1"; to: 1; duration: 220; easing.type: tb.popEase; easing.overshoot: 2.4 }
        SequentialAnimation {
            PauseAnimation { duration: 55 }
            NumberAnimation { target: tb; property: "d2"; to: 1; duration: 220; easing.type: tb.popEase; easing.overshoot: 2.4 }
        }
        SequentialAnimation {
            PauseAnimation { duration: 110 }
            NumberAnimation { target: tb; property: "d3"; to: 1; duration: 230; easing.type: tb.popEase; easing.overshoot: 2.2 }
        }
        SequentialAnimation {
            PauseAnimation { duration: 150 }
            NumberAnimation { target: tb; property: "bp"; to: 1; duration: 420; easing.type: tb.popEase; easing.overshoot: 1.5 }
        }
    }
    ParallelAnimation {
        id: popOut
        NumberAnimation { target: tb; property: "bp"; to: 0; duration: 200; easing.type: Easing.InBack; easing.overshoot: 1.2 }
        SequentialAnimation {
            PauseAnimation { duration: 110 }
            NumberAnimation { target: tb; property: "d3"; to: 0; duration: 130; easing.type: Easing.InQuad }
        }
        SequentialAnimation {
            PauseAnimation { duration: 160 }
            NumberAnimation { target: tb; property: "d2"; to: 0; duration: 130; easing.type: Easing.InQuad }
        }
        SequentialAnimation {
            PauseAnimation { duration: 210 }
            NumberAnimation { target: tb; property: "d1"; to: 0; duration: 130; easing.type: Easing.InQuad }
        }
        onFinished: {
            tb.closing = false
            tb.phase = "hidden"
            tb.tokens = []
            tb.revealed = 0
            tb.longThink = false
            tb.maskBody = Qt.rect(0, 0, 0, 0)
        }
    }

    // ---- ambient motion -----------------------------------------------------------------
    // thinkT drives the thinking dots and the thought trail; breathT a calm
    // ~4 s breathing of the whole bubble while it thinks.
    property real thinkT: 0
    property real breathT: 0
    NumberAnimation on thinkT {
        from: 0; to: 1; duration: 1700; loops: Animation.Infinite
        running: tb.phase === "thinking" && tb.visible
    }
    NumberAnimation on breathT {
        from: 0; to: 1; duration: 4200; loops: Animation.Infinite
        running: tb.phase === "thinking" && tb.visible
    }
    readonly property real breath: phase === "thinking"
                                   ? 1 + 0.024 * mo * Math.sin(breathT * 2 * Math.PI) : 1
    function wave(i) {
        return Math.max(0, Math.sin(thinkT * 2 * Math.PI - i * 0.95))
    }

    property real shakeX: 0
    SequentialAnimation {
        id: shake
        NumberAnimation { target: tb; property: "shakeX"; to: -6 * tb.mo; duration: 55; easing.type: Easing.OutQuad }
        NumberAnimation { target: tb; property: "shakeX"; to: 5 * tb.mo; duration: 70; easing.type: Easing.InOutQuad }
        NumberAnimation { target: tb; property: "shakeX"; to: -3 * tb.mo; duration: 70; easing.type: Easing.InOutQuad }
        NumberAnimation { target: tb; property: "shakeX"; to: 1.5 * tb.mo; duration: 70; easing.type: Easing.InOutQuad }
        NumberAnimation { target: tb; property: "shakeX"; to: 0; duration: 90; easing.type: Easing.OutQuad }
    }

    // A soft swell as an answer lands, so its arrival reads as an event.
    property real arrival: 0
    SequentialAnimation {
        id: arrivalPulse
        NumberAnimation { target: tb; property: "arrival"; to: 0.016 * tb.mo; duration: 150; easing.type: Easing.OutQuad }
        NumberAnimation { target: tb; property: "arrival"; to: 0; duration: 520; easing.type: Easing.OutBack; easing.overshoot: 2.2 }
    }

    // Answer content fades up once the bubble has had a moment to grow.
    property real contentIn: 0
    property real footIn: 0
    ParallelAnimation {
        id: entrance
        SequentialAnimation {
            PropertyAction { target: tb; property: "contentIn"; value: 0 }
            PauseAnimation { duration: 70 }
            NumberAnimation { target: tb; property: "contentIn"; to: 1; duration: 360; easing.type: Easing.OutCubic }
        }
        SequentialAnimation {
            PropertyAction { target: tb; property: "footIn"; value: 0 }
            PauseAnimation { duration: 300 }
            NumberAnimation { target: tb; property: "footIn"; to: 1; duration: 360; easing.type: Easing.OutCubic }
        }
    }

    // History rows slide in one after another.
    property real histIn: 0
    NumberAnimation {
        id: historyIn
        target: tb
        property: "histIn"
        from: 0
        to: 1
        duration: 620
        easing.type: Easing.OutCubic
    }
    // "Clear" asks once more before it wipes everything.
    property bool clearArmed: false
    Timer { id: clearDisarm; interval: 2800; onTriggered: tb.clearArmed = false }

    // Long waits get a few quiet words beside the dots.
    property bool longThink: false
    property int thinkStep: 0
    readonly property var thinkPhrases: [qsTr("Thinking it through…"), qsTr("Connecting ideas…"),
                                         qsTr("Choosing my words…"), qsTr("Almost there…")]
    onPhaseChanged: {
        if (phase === "thinking") {
            longThink = false
            thinkStep = 0
            longThinkTimer.restart()
        } else {
            longThinkTimer.stop()
            longThink = false
        }
    }
    Timer { id: longThinkTimer; interval: 2600; onTriggered: tb.longThink = true }
    Timer {
        interval: 2600; repeat: true
        running: tb.longThink && tb.phase === "thinking" && tb.visible
        onTriggered: tb.thinkStep = (tb.thinkStep + 1) % tb.thinkPhrases.length
    }

    Timer {
        id: noteTimer
        interval: 2600
        onTriggered: {
            if (bodyHover.hovered) {
                restart()
                return
            }
            tb.close()
            tb.dismissed()
        }
    }

    // ---- word cascade -------------------------------------------------------------------
    property var tokens: []
    property int revealed: 0
    property bool finishedSent: false
    readonly property int revealInterval: 46
    readonly property int revealStep: Math.max(1, Math.round(tokens.length
        / (Math.max(650, Math.min(3600, tokens.length * 48)) / revealInterval)))

    Timer {
        id: revealTimer
        interval: tb.revealInterval
        repeat: true
        running: tb.revealing && tb.contentIn > 0.04 && tb.visible && !tb.closing
        onTriggered: {
            const n = tb.tokens.length
            let next = Math.min(n, tb.revealed + tb.revealStep)
            while (next < n && tb.tokens[next].br)      // line breaks cost no time
                ++next
            tb.revealed = next
            tb.speakingTick(n > 0 ? next / n : 1)
            if (BuddySound && Math.random() < 0.26)
                BuddySound.play("speak")
            tb.followReveal()
        }
    }
    onRevealedChanged: {
        trackGrowth()
        if (phase === "answer" && kind === "chat" && tokens.length > 0
                && revealed >= tokens.length && !finishedSent) {
            finishedSent = true
            followReveal()
            if (hasShots)
                scrollSoon.restart()
            followField.input.forceActiveFocus()
            revealFinished()
        }
    }
    function followReveal() {
        if (chatFlick.contentHeight <= chatFlick.height + 1)
            return
        const item = words.itemAt(Math.max(0, revealed - 1))
        if (!item)
            return
        const want = Math.min(chatFlick.contentHeight - chatFlick.height,
                              flow.y + item.y + item.height + 6 - chatFlick.height)
        if (want > chatFlick.contentY)
            chatFlick.contentY = want
    }

    // A small markdown-lite tokenizer: paragraphs, "- " / "1." bullets,
    // headings, **bold**, `code` and fenced code lines become styled word
    // tokens; anything else is shown exactly as written.
    function tokenize(text) {
        const out = []
        const src = String(text || "").replace(/\r\n?/g, "\n").trim()
        const lines = src.length > 0 ? src.split("\n") : []
        let inCode = false
        let blank = false
        const lastIsBreak = () => out.length === 0 || !!out[out.length - 1].br
        for (let li = 0; li < lines.length; ++li) {
            const line = lines[li]
            const trimmed = line.trim()
            if (trimmed.indexOf("```") === 0) {
                inCode = !inCode
                if (!lastIsBreak())
                    out.push({ br: true, gap: 2 })
                continue
            }
            if (inCode) {
                if (trimmed.length > 0) {
                    out.push({ t: line.replace(/\t/g, "    ").replace(/\s+$/, ""), c: true })
                    out.push({ br: true, gap: 0 })
                }
                continue
            }
            if (trimmed.length === 0) {
                if (out.length > 0 && !blank) {
                    if (lastIsBreak())
                        out[out.length - 1].gap = 4
                    else
                        out.push({ br: true, gap: 4 })
                    blank = true
                }
                continue
            }
            blank = false
            if (!lastIsBreak())
                out.push({ br: true, gap: 0 })

            let body = trimmed
            let bold = false
            const heading = body.match(/^#{1,6}\s+(.*)$/)
            if (heading) {
                body = heading[1]
                bold = true
            }
            const bullet = body.match(/^[-*•]\s+(.*)$/)
            const numbered = bullet ? null : body.match(/^(\d+[.)])\s+(.*)$/)
            if (bullet) {
                out.push({ t: "•", m: true })
                body = bullet[1]
            } else if (numbered) {
                out.push({ t: numbered[1], m: true })
                body = numbered[2]
            }

            const parts = body.split(/(\*\*[^*]+\*\*|__[^_]+__|`[^`]+`)/)
            for (let pi = 0; pi < parts.length; ++pi) {
                let part = parts[pi]
                if (!part)
                    continue
                let b = bold, c = false
                if (/^\*\*[^*]+\*\*$/.test(part) || /^__[^_]+__$/.test(part)) {
                    part = part.slice(2, -2)
                    b = true
                } else if (/^`[^`]+`$/.test(part)) {
                    part = part.slice(1, -1)
                    c = true
                }
                const ws = part.split(/\s+/)
                for (let wi = 0; wi < ws.length; ++wi) {
                    let word = ws[wi]
                    if (!c) {
                        // *single-star emphasis*: drop the stars, keep the word.
                        if (word.length > 1 && word.charAt(0) === "*")
                            word = word.slice(1)
                        if (word.length > 1 && /\*[.,!?;:)]*$/.test(word))
                            word = word.replace(/\*([.,!?;:)]*)$/, "$1")
                    }
                    if (word.length > 0)
                        out.push({ t: word, b: b, c: c })
                }
            }
        }
        while (out.length > 0 && out[out.length - 1].br)
            out.pop()

        // Very long replies: animate small groups of words instead of single
        // ones so the cascade stays light.
        const limit = 420
        if (out.length <= limit)
            return out
        const group = Math.ceil(out.length / limit)
        const merged = []
        for (let i = 0; i < out.length; ++i) {
            const tk = out[i]
            const prev = merged.length > 0 ? merged[merged.length - 1] : null
            if (prev && !prev.br && !tk.br && !prev.m && !tk.m && (prev.n || 1) < group
                    && prev.b === tk.b && prev.c === tk.c) {
                prev.t += " " + tk.t
                prev.n = (prev.n || 1) + 1
            } else {
                merged.push(Object.assign({}, tk))
            }
        }
        return merged
    }


    // ---- the thought trail ---------------------------------------------------------------
    Repeater {
        model: [{ f: 0.16, size: 4.5 }, { f: 0.47, size: 6.5 }, { f: 0.80, size: 9 }]

        Rectangle {
            id: dot
            required property var modelData
            required property int index
            readonly property real prog: index === 0 ? tb.d1 : (index === 1 ? tb.d2 : tb.d3)
            readonly property real cx: tb.tailStartX + (tb.tailEndX - tb.tailStartX) * modelData.f
                                       + (tb.centered ? Math.sin(modelData.f * Math.PI) * 2.5 * tb.s : 0)
            readonly property real cy: tb.tailStartY + (tb.tailEndY - tb.tailStartY) * modelData.f
                                       + (tb.centered ? 0 : -tb.vDir * Math.sin(modelData.f * Math.PI) * 3 * tb.s)
            // While thinking, a pulse travels from the orb out to the bubble.
            readonly property real pulse: tb.phase === "thinking"
                                          ? Math.max(0, Math.sin(tb.thinkT * 2 * Math.PI - index * 1.1)) : 0
            width: Math.round(modelData.size * tb.s)
            height: width
            radius: width / 2
            x: Math.round(cx - width / 2)
            y: Math.round(cy - height / 2)
            visible: prog > 0.001
            opacity: Math.min(1, prog * 1.6)
            scale: Math.max(0, prog) * (1 + 0.22 * pulse * tb.mo)
            color: tb.buddySurface
            border.width: 1
            border.color: tb.phase === "thinking"
                          ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35 + 0.5 * pulse)
                          : Theme.borderStrong
            antialiasing: true
        }
    }

    // ---- the bubble -------------------------------------------------------------------------
    Item {
        id: body
        x: tb.bx
        y: tb.by
        width: tb.w
        height: tb.h
        visible: tb.bp > 0.001
        opacity: Math.min(1, tb.bp * 1.7)

        transform: [
            Scale {
                origin.x: tb.centered ? body.width / 2 : (tb.flipped ? body.width : 0)
                origin.y: tb.centered ? (tb.above ? body.height : 0)
                                      : (tb.above ? body.height - Math.min(body.height, tb.rowH) / 2
                                                  : Math.min(body.height, tb.rowH) / 2)
                xScale: (0.3 + 0.7 * tb.bp) * tb.breath * (1 + tb.arrival)
                yScale: (0.3 + 0.7 * tb.bp) * tb.breath * (1 + tb.arrival)
            },
            Translate { x: tb.shakeX }
        ]

        HoverHandler { id: bodyHover }

        Rectangle {
            id: surface
            anchors.fill: parent
            radius: Math.min(height / 2, 22)
            color: tb.buddySurface
            border.width: tb.phase === "input" ? 0 : 1
            border.color: tb.phase === "thinking"
                          ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30 + 0.30 * Math.sin(tb.breathT * 2 * Math.PI) * tb.mo)
                          : (tb.phase === "error" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.55)
                          : (tb.revealing ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                          : (field.activeFocus && tb.phase === "input"
                             ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55) : Theme.border)))
            Behavior on border.color { ColorAnimation { duration: 220 } }
            antialiasing: true
        }

        // A faint top sheen so the bubble reads as a soft, rounded thing.
        Rectangle {
            x: Math.round(surface.radius * 0.6)
            y: 1
            width: Math.max(0, parent.width - 2 * x)
            height: Math.min(14, parent.height * 0.3)
            radius: height / 2
            opacity: 0.045
            gradient: Gradient {
                GradientStop { position: 0; color: "#ffffff" }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        Item {
            id: stage
            anchors.fill: parent
            clip: true

            // ---- input ---------------------------------------------------------
            Item {
                id: inputPane
                width: tb.inputW
                height: tb.rowH
                x: tb.paneX(width)
                opacity: tb.phase === "input" ? 1 : 0
                visible: opacity > 0.01
                enabled: tb.phase === "input"
                Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                transform: Translate {
                    y: tb.phase === "input" ? 0 : -6
                    Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }

                Glyph {
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    name: "spark"
                    size: 15
                    color: field.activeFocus ? Theme.accent : Theme.textFaint
                    Behavior on color { ColorAnimation { duration: 160 } }
                }

                TextInput {
                    id: field
                    anchors.left: parent.left
                    anchors.leftMargin: 38
                    anchors.right: micBtn.left
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.inverse
                    font.family: Theme.font
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.Medium
                    clip: true
                    selectByMouse: true
                    onTextEdited: tb.typed(width > 0 ? Math.min(1, cursorRectangle.x / width) : 0)
                    onCursorPositionChanged: if (activeFocus && text.length > 0)
                                                 tb.typed(width > 0 ? Math.min(1, cursorRectangle.x / width) : 0)

                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Ask Noch's buddy")
                    Accessible.description: qsTr("Type a request, then press Enter")

                    Keys.onReturnPressed: (event) => { tb.trySubmit(); event.accepted = true }
                    Keys.onEnterPressed: (event) => { tb.trySubmit(); event.accepted = true }
                    Keys.onEscapePressed: (event) => { tb.close(); tb.dismissed(); event.accepted = true }
                    // Up on an empty field opens the history, like a shell.
                    Keys.onUpPressed: (event) => {
                        if (field.text.length === 0) {
                            tb.showHistory()
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }

                    Text {
                        anchors.fill: parent
                        visible: field.text.length === 0
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("What should I help with?")
                        color: Theme.textFaint
                        font: field.font
                        elide: Text.ElideRight
                    }
                }

                // Hold to talk. Only there once voice input is switched on in Settings.
                BuddyMicButton {
                    id: micBtn
                    anchors.right: histBtn.left
                    anchors.verticalCenter: parent.verticalCenter
                }
                BuddyIconButton {
                    id: histBtn
                    anchors.right: sendBtn.left
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "history"
                    tip: qsTr("History")
                    tint: Theme.textFaint
                    onClicked: tb.showHistory()
                }
                BuddySendButton {
                    id: sendBtn
                    anchors.right: xBtn.left
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    tip: qsTr("Send request")
                    active: field.text.trim().length > 0
                    onClicked: tb.trySubmit()
                }
                BuddyIconButton {
                    id: xBtn
                    anchors.right: parent.right
                    anchors.rightMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    icon: "close"
                    tip: qsTr("Dismiss")
                    tint: Theme.textFaint
                    onClicked: { tb.close(); tb.dismissed() }
                }
            }

            // ---- history -------------------------------------------------------
            Item {
                id: historyPane
                width: tb.histW
                height: parent.height
                x: tb.paneX(width)
                opacity: tb.phase === "history" ? 1 : 0
                visible: opacity > 0.01
                enabled: tb.phase === "history"
                Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                Item {
                    id: histHeader
                    width: parent.width
                    height: 44

                    BuddyIconButton {
                        id: histBack
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "left"
                        tip: qsTr("Back")
                        onClicked: tb.backToInput()
                    }
                    Text {
                        id: histTitle
                        anchors.left: histBack.right
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("History")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.fontMd
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.left: histTitle.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tb.history.length > 0
                        width: histCount.implicitWidth + 12
                        height: 18
                        radius: 9
                        color: Theme.glassFill
                        border.width: 0
                        border.color: Theme.glassBorder
                        Text {
                            id: histCount
                            anchors.centerIn: parent
                            text: tb.history.length
                            color: Theme.textDim
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontXs
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        visible: tb.history.length > 0
                        width: clearRow.implicitWidth + 18
                        height: 26
                        radius: 13
                        color: tb.clearArmed ? Theme.dangerSoft
                                             : (clearHit.containsMouse ? Theme.glassFillHover : "transparent")
                        border.width: 1
                        border.color: tb.clearArmed ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.5) : Theme.glassBorder
                        Behavior on color { ColorAnimation { duration: 140 } }
                        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Row {
                            id: clearRow
                            anchors.centerIn: parent
                            spacing: 5
                            Glyph {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "trash"
                                size: 12
                                stroke: 1.7
                                color: tb.clearArmed ? Theme.danger : Theme.textDim
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tb.clearArmed ? qsTr("Clear all?") : qsTr("Clear")
                                color: tb.clearArmed ? Theme.danger : Theme.textDim
                                font.family: Theme.font
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: clearHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Clear history")
                            onClicked: {
                                if (tb.clearArmed) {
                                    tb.clearArmed = false
                                    tb.historyCleared()
                                } else {
                                    tb.clearArmed = true
                                    clearDisarm.restart()
                                }
                            }
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        x: 12
                        width: parent.width - 24
                        height: 1
                        color: Theme.glassBorder
                    }
                }

                ListView {
                    id: histList
                    x: 6
                    y: histHeader.height + 4
                    width: parent.width - 12
                    height: parent.height - y - 4
                    clip: true
                    model: tb.history
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    currentIndex: -1
                    highlightFollowsCurrentItem: false
                    Keys.onReturnPressed: tb.pickHistory(currentIndex)
                    Keys.onEnterPressed: tb.pickHistory(currentIndex)
                    Keys.onEscapePressed: tb.backToInput()

                    delegate: Item {
                        id: hrow
                        required property var modelData
                        required property int index
                        readonly property bool current: ListView.isCurrentItem
                        // Staggered entrance: each row starts a little after the one above.
                        readonly property real enter: Math.max(0, Math.min(1, tb.histIn * 1.7 - Math.min(index, 8) * 0.09))
                        width: histList.width
                        height: tb.histRowH
                        opacity: enter
                        transform: Translate { x: (1 - hrow.enter) * 12 * tb.side * tb.mo }

                        readonly property bool lit: hrow.current || rowHit.containsMouse
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: Theme.radiusMd
                            color: hrow.lit ? Theme.glassFillHover : "transparent"
                            border.width: hrow.lit ? 1 : 0
                            border.color: Theme.glassBorder
                        }
                        Rectangle {
                            id: kindTile
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            height: 32
                            radius: Theme.radiusMd
                            color: hrow.lit ? Theme.glassFillActive : Theme.glassFill
                            border.width: 0
                            border.color: Theme.glassBorder
                            Glyph {
                                anchors.centerIn: parent
                                name: tb.kindIcon(hrow.modelData.kind)
                                size: 15
                                color: hrow.lit ? Theme.ink : Theme.textDim
                            }
                        }
                        Text {
                            id: when
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            y: 10
                            text: tb.timeAgo(hrow.modelData.t)
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontXs
                        }
                        Text {
                            anchors.left: kindTile.right
                            anchors.leftMargin: 11
                            anchors.right: when.left
                            anchors.rightMargin: 10
                            y: 9
                            text: String(hrow.modelData.q || "")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        Text {
                            anchors.left: kindTile.right
                            anchors.leftMargin: 11
                            anchors.right: openHint.left
                            anchors.rightMargin: 8
                            y: 28
                            text: hrow.modelData.kind === "chat"
                                  ? String(hrow.modelData.a || "").replace(/[*_`#>]+/g, "").replace(/\s+/g, " ").trim()
                                  : String(hrow.modelData.title || "")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        Glyph {
                            id: openHint
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            y: 29
                            name: "right"
                            size: 12
                            color: Theme.textFaint
                            opacity: hrow.lit ? 1 : 0
                        }
                        MouseArea {
                            id: rowHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: String(hrow.modelData.q || "")
                            onEntered: histList.currentIndex = hrow.index
                            onClicked: tb.pickHistory(hrow.index)
                        }
                    }
                }

                // Empty state.
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: histHeader.height + 14
                    spacing: 6
                    visible: tb.history.length === 0
                    opacity: tb.histIn
                    Glyph {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "history"
                        size: 20
                        color: Theme.textFaint
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Nothing here yet — ask me something!")
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontBase
                    }
                }
            }

            // ---- thinking ------------------------------------------------------
            Item {
                id: thinkPane
                width: Math.max(tb.thinkW, 84)
                height: tb.thinkH
                x: tb.paneX(width)
                opacity: tb.phase === "thinking" ? 1 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                Item {
                    id: thinkTopRow
                    width: parent.width
                    height: tb.rowH

                    Row {
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Repeater {
                            model: 3
                            Rectangle {
                                id: tdot
                                required property int index
                                readonly property real lift: tb.wave(index)
                                width: 6.5
                                height: 6.5
                                radius: 3.25
                                color: Theme.accent
                                opacity: 0.32 + 0.68 * lift
                                scale: 0.82 + 0.22 * lift
                                transform: Translate { y: -3.0 * tb.mo * tdot.lift }
                                antialiasing: true
                            }
                        }
                    }

                    // Active agent tool while thinking: icon · BADGE · summary · #n
                    Item {
                        x: 64
                        width: parent.width - 78
                        height: parent.height
                        visible: tb.hasTools || tb.activeToolSummary.length > 0

                        Glyph {
                            id: activeToolIcon
                            anchors.verticalCenter: parent.verticalCenter
                            name: tb.toolIcon(tb.activeTool)
                            size: 15
                            color: Theme.ink
                        }

                        Text {
                            id: activeBadgeText
                            anchors.left: activeToolIcon.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: tb.activeToolBadge.length > 0
                            text: tb.activeToolBadge
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontXs - 1
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                        }

                        Text {
                            anchors.left: activeBadgeText.visible ? activeBadgeText.right : activeToolIcon.right
                            anchors.leftMargin: 8
                            anchors.right: stepCountText.visible ? stepCountText.left : parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: tb.activeToolSummary.length > 0
                                  ? tb.activeToolSummary
                                  : qsTr("Working with tools…")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Row {
                            id: stepCountText
                            visible: tb.hasTools || tb.specialist.length > 0
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            // Which specialist owns the run after a handoff.
                            Rectangle {
                                visible: tb.specialist.length > 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: specialistRow.implicitWidth + 14
                                height: 20
                                radius: 10
                                color: Theme.tile
                                border.width: 0
                                border.color: Theme.border
                                Row {
                                    id: specialistRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "handoff"
                                        size: 11
                                        color: Theme.accent
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tb.specialist
                                        color: Theme.ink
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontXs
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                            Text {
                                visible: tb.hasTools
                                anchors.verticalCenter: parent.verticalCenter
                                text: tb.toolSteps.length === 1 ? qsTr("1 step") : qsTr("%1 steps").arg(tb.toolSteps.length)
                                color: Theme.textFaint
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontXs
                            }
                        }
                    }

                    Item {
                        x: 84
                        width: thinkLabel.implicitWidth
                        height: parent.height
                        visible: tb.showThinking && !tb.hasTools && tb.activeToolSummary.length === 0
                        opacity: tb.longThink ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.InOutSine } }

                        Text {
                            id: thinkLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: tb.thinkPhrases[tb.thinkStep]
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.Medium
                            onTextChanged: phraseFade.restart()
                            NumberAnimation {
                                id: phraseFade
                                target: thinkLabel; property: "opacity"
                                from: 0.2; to: 1; duration: 560; easing.type: Easing.OutCubic
                            }
                        }
                    }
                }

                // Hairline above the live tool timeline
                Rectangle {
                    visible: tb.hasTools
                    x: 14
                    y: tb.rowH - 2
                    width: parent.width - 28
                    height: 1
                    color: Theme.glassBorder
                }

                // Live tool timeline (last four steps)
                Column {
                    id: liveTimeline
                    x: 10
                    y: tb.rowH + 4
                    width: parent.width - 20
                    spacing: 3
                    visible: tb.hasTools

                    Repeater {
                        model: tb.hasTools ? tb.toolSteps.slice(-4) : []
                        ToolStepRow {
                            required property var modelData
                            width: parent.width
                            step: modelData
                        }
                    }
                }

                // Live answer: streamed text in a transparent well that keeps
                // the newest line in view, with a soft caret.
                Rectangle {
                    id: liveWell
                    visible: tb.hasLive
                    x: 10
                    y: tb.thinkH - tb.liveH - 8
                    width: parent.width - 20
                    height: tb.liveH
                    radius: Theme.radiusMd
                    color: Theme.glassFill
                    border.width: 0
                    border.color: Theme.glassBorder
                    clip: true

                    Text {
                        id: liveBody
                        x: 12
                        width: parent.width - 24
                        // Pin to the bottom once the text outgrows the well.
                        y: Math.min(11, parent.height - 11 - implicitHeight)
                        text: tb.liveText + " \u258F"
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.font
                        font.pixelSize: Theme.fontBase + 1
                        lineHeight: 1.18
                        wrapMode: Text.Wrap
                    }
                    // Fade the clipped top edge into the glass.
                    Rectangle {
                        visible: liveBody.y < 0
                        width: parent.width
                        height: 22
                        gradient: Gradient {
                            GradientStop { position: 0; color: Theme.surface }
                            GradientStop { position: 1; color: "transparent" }
                        }
                    }
                }

                // Human-in-the-loop: the agent is paused on a tool call.
                ToolApprovalCard {
                    id: approvalCard
                    x: 10
                    y: tb.hasTools ? tb.rowH + 4 + Math.min(tb.toolSteps.length, 4) * 31 + 4 : tb.rowH
                    width: parent.width - 20
                    visible: tb.awaitingApproval && tb.phase === "thinking"
                    request: tb.pendingApproval
                    onDecided: (approve, always) => {
                        if (BuddyAgent)
                            BuddyAgent.resolveApproval(approve, always)
                    }
                }

                // Buddy would rather ask than guess.
                BuddyQuestionCard {
                    id: questionCard
                    x: 10
                    y: tb.hasTools ? tb.rowH + 4 + Math.min(tb.toolSteps.length, 4) * 31 + 4 : tb.rowH
                    width: parent.width - 20
                    maxCardHeight: Math.max(160, tb.maxH - 120)
                    visible: tb.awaitingQuestion && tb.phase === "thinking"
                    request: tb.pendingQuestion
                    onAnswered: (answers) => {
                        if (BuddyAgent)
                            BuddyAgent.answerQuestion(answers)
                    }
                    onSkipped: {
                        if (BuddyAgent)
                            BuddyAgent.skipQuestion()
                    }
                }
            }

            // ---- chat answer ---------------------------------------------------
            Item {
                id: chatPane
                width: tb.chatW
                height: parent.height
                x: tb.paneX(width)
                opacity: tb.phase === "answer" && tb.kind === "chat" ? 1 : 0
                visible: opacity > 0.01
                enabled: tb.phase === "answer" && tb.kind === "chat"
                Behavior on opacity { NumberAnimation { duration: 200 } }

                // Redesigned Agent SDK execution trace header + collapsible timeline drawer
                Item {
                    id: chatToolsHeader
                    x: tb.pad
                    y: 10
                    width: tb.chatW - 2 * tb.pad
                    height: tb.chatHeaderH
                    visible: tb.hasTools

                    ToolTraceBar {
                        id: toolSummaryBar
                        width: parent.width
                        undoSource: BuddyAgent
                        steps: tb.toolSteps
                        icons: tb.distinctToolIcons
                        durationMs: tb.responseMeta ? Number(tb.responseMeta.durationMs || 0) : 0
                        expanded: tb.toolsExpanded
                        onToggled: tb.toolsExpanded = !tb.toolsExpanded
                    }

                    Item {
                        id: toolDrawer
                        y: 34
                        width: parent.width
                        height: tb.toolDrawerH
                        visible: tb.hasTools && tb.toolsExpanded
                        clip: true

                        Flickable {
                            anchors.fill: parent
                            contentWidth: width
                            contentHeight: toolDrawerCol.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            interactive: contentHeight > height + 1
                            clip: true

                            Column {
                                id: toolDrawerCol
                                width: parent.width
                                spacing: 5

                                Repeater {
                                    model: tb.toolSteps
                                    ToolStepCard {
                                        required property var modelData
                                        width: toolDrawerCol.width
                                        step: modelData
                                    }
                                }
                            }
                        }
                    }
                }

                Flickable {
                    id: chatFlick
                    x: tb.pad
                    y: 12 + tb.chatHeaderH
                    width: tb.chatW - 2 * tb.pad
                    height: tb.chatContentH
                    contentWidth: width
                    contentHeight: chatBodyCol.implicitHeight
                    interactive: contentHeight > height + 1
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    // The text settles into place a beat after the card has grown.
                    transform: Translate { y: (1 - tb.contentIn) * 7 }
                    Behavior on contentY {
                        enabled: tb.revealing
                        NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
                    }

                    Column {
                        id: chatBodyCol
                        width: chatFlick.width
                        spacing: 8

                        // Anthropic Advisor Strategy Callout Card
                        Rectangle {
                            visible: tb.hasAdvisor
                            width: parent.width
                            height: advisorInner.implicitHeight + 14
                            radius: 9
                            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                            border.width: 0
                            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)

                            Column {
                                id: advisorInner
                                x: 10
                                y: 7
                                width: parent.width - 20
                                spacing: 3

                                Row {
                                    spacing: 5
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "advisor"
                                        size: 11.5
                                        stroke: 1.8
                                        color: Theme.accent
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("ADVISOR STRATEGY · %1")
                                              .arg(String(tb.responseMeta && tb.responseMeta.advisorFocus ? tb.responseMeta.advisorFocus : "GENERAL").toUpperCase())
                                        color: Theme.accent
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontXs
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.5
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: String(tb.responseMeta && tb.responseMeta.advisorInsight ? tb.responseMeta.advisorInsight : "")
                                          .replace(/[#*`]/g, "").trim()
                                    color: Theme.inkSoft
                                    font.family: Theme.font
                                    font.pixelSize: Theme.fontSm
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    lineHeight: 1.15
                                }
                            }
                        }

                        // Anthropic TodoWrite Live Checklist Card
                        Rectangle {
                            visible: tb.hasAgentTodos
                            width: parent.width
                            height: agentTodoCol.implicitHeight + 14
                            radius: 9
                            color: Theme.tile
                            border.width: 0
                            border.color: Theme.borderSoft

                            Column {
                                id: agentTodoCol
                                x: 10
                                y: 7
                                width: parent.width - 20
                                spacing: 4

                                Row {
                                    spacing: 5
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "tasks"
                                        size: 11
                                        stroke: 1.8
                                        color: Theme.accent
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("AGENT TASKS (%1)").arg(tb.agentTodos.length)
                                        color: Theme.textDim
                                        font.family: Theme.font
                                        font.pixelSize: Theme.fontXs
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.5
                                    }
                                }

                                Repeater {
                                    model: tb.agentTodos
                                    Row {
                                        id: todoRow
                                        required property var modelData
                                        width: agentTodoCol.width
                                        spacing: 6
                                        readonly property bool done: modelData.status === "completed"
                                        readonly property bool active: modelData.status === "in_progress"

                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 13
                                            height: 13
                                            radius: 4
                                            color: todoRow.done ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.18)
                                                 : (todoRow.active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent")
                                            border.width: 1
                                            border.color: todoRow.done ? Theme.success : (todoRow.active ? Theme.accent : Theme.borderStrong)

                                            Glyph {
                                                visible: todoRow.done || todoRow.active
                                                anchors.centerIn: parent
                                                name: todoRow.done ? "check" : "bolt"
                                                size: 8.5
                                                stroke: 2.0
                                                color: todoRow.done ? Theme.success : Theme.accent
                                            }
                                        }

                                        Text {
                                            width: parent.width - 20
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: String(todoRow.modelData.content || "")
                                            color: parent.done ? Theme.textDim : Theme.ink
                                            font.family: Theme.font
                                            font.pixelSize: Theme.fontSm
                                            font.strikeout: parent.done
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }

                        Flow {
                            id: flow
                            width: chatBodyCol.width
                            spacing: 4

                            Repeater {
                                id: words
                                model: tb.tokens

                                Text {
                                    id: word
                                    required property var modelData
                                    required property int index
                                    readonly property bool isBreak: !!modelData.br
                                    readonly property bool lit: index < tb.revealed
                                    // Flow skips zero-sized items, so a line break is a
                                    // hair-thin full-width row; paragraphs get a real gap.
                                    width: isBreak ? flow.width : Math.min(implicitWidth, flow.width)
                                    height: isBreak ? Math.max(0.5, modelData.gap || 0)
                                                    : (modelData.c ? Math.max(implicitHeight, bodyMetrics.height)
                                                                   : implicitHeight)
                                    // Inline code sits on the same baseline as the words around it.
                                    topPadding: modelData.c ? Math.max(0, bodyMetrics.ascent - codeMetrics.ascent) : 0
                                    text: isBreak ? "" : modelData.t
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    font.family: modelData.c ? "Consolas" : Theme.font
                                    font.pixelSize: modelData.c ? 12 : 13
                                    font.weight: modelData.b ? Font.DemiBold : Font.Normal
                                    // Each word arrives in the accent colour and
                                    // settles into ink: a soft trail of light.
                                    color: !lit ? Theme.accent
                                           : (modelData.m ? Theme.accent : (modelData.c ? Theme.inkSoft : Theme.ink))
                                    Behavior on color { ColorAnimation { duration: 520; easing.type: Easing.OutCubic } }
                                    opacity: lit ? 1 : 0
                                    // On the render thread: fading a few words per tick costs the GUI thread nothing.
                                    Behavior on opacity { OpacityAnimator { duration: 280; easing.type: Easing.OutCubic } }
                                    transform: Translate {
                                        y: word.lit ? 0 : 4 * tb.mo
                                        Behavior on y { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                                    }
                                }
                            }
                        }

                        // Screenshots Buddy shared, once the words are in.
                        Column {
                            id: shotsCol
                            width: chatBodyCol.width
                            spacing: 8
                            visible: tb.attachments.length > 0 && !tb.revealing
                            Repeater {
                                model: tb.attachments
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

                        // Clickable Web Search & Fetch Citations
                        Flow {
                            visible: tb.hasCitations && !tb.revealing
                            width: parent.width
                            spacing: 5

                            Repeater {
                                model: tb.citations ? tb.citations.slice(0, 4) : []

                                Rectangle {
                                    id: citeChip
                                    required property var modelData
                                    width: Math.min(chatBodyCol.width, citeRow.implicitWidth + 12)
                                    height: 20
                                    radius: 10
                                    color: citeHit.containsMouse ? Theme.hover : Theme.tile
                                    border.width: 0
                                    border.color: citeHit.containsMouse ? Theme.accent : Theme.borderSoft

                                    Row {
                                        id: citeRow
                                        x: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 4

                                        Glyph {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "globe"
                                            size: 10
                                            stroke: 1.7
                                            color: Theme.accent
                                        }

                                        Text {
                                            width: Math.min(130, implicitWidth)
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: String(citeChip.modelData.title || citeChip.modelData.url || "")
                                            color: Theme.inkSoft
                                            font.family: Theme.font
                                            font.pixelSize: Theme.fontXs
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        id: citeHit
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (citeChip.modelData.url) Qt.openUrlExternally(citeChip.modelData.url)
                                    }
                                }
                            }
                        }

                        // Long answers: a quiet note when the reply needed
                        // "continue" requests, or was still cut off after them.
                        Text {
                            readonly property var meta: tb.responseMeta || {}
                            readonly property bool cut: !!meta.truncated
                            readonly property int parts: Number(meta.continuations || 0)
                            width: chatBodyCol.width
                            visible: !tb.simulated && !tb.revealing && (cut || parts > 0)
                            text: cut ? qsTr("Reply hit the length limit and may be cut off. Ask me to continue.")
                                      : qsTr("Long reply, continued %n time(s)", "", parts)
                            color: cut ? Theme.warning : Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // Anthropic Agent SDK Telemetry Footer Pill
                        Row {
                            visible: tb.hasSdkTelemetry && !tb.revealing
                            spacing: 6

                            Rectangle {
                                height: 18
                                width: sdkMetaRow.implicitWidth + 12
                                radius: 5
                                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.04)
                                border.width: 0
                                border.color: Theme.borderSoft

                                Row {
                                    id: sdkMetaRow
                                    anchors.centerIn: parent
                                    spacing: 5

                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "bolt"
                                        size: 9.5
                                        stroke: 1.8
                                        color: Theme.accent
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: {
                                            const m = tb.responseMeta || {}
                                            const parts = []
                                            if (m.model) parts.push(String(m.model))
                                            if (Number(m.turns || 0) > 1) parts.push(m.turns + " turns")
                                            if (Number(m.handoffs || 0) > 0) parts.push(m.handoffs === 1 ? "1 handoff" : m.handoffs + " handoffs")
                                            if (Number(m.totalTokens || 0) > 0) parts.push(m.totalTokens + " tok")
                                            if (Number(m.durationMs || 0) > 0) parts.push(tb.formatDuration(m.durationMs))
                                            return parts.join(" · ")
                                        }
                                        color: Theme.textFaint
                                        font.family: Theme.monoFont
                                        font.pixelSize: Theme.fontXs
                                    }
                                }
                            }
                        }
                    }
                }

                // Tap the text to skip the cascade.
                MouseArea {
                    anchors.fill: chatFlick
                    enabled: tb.revealing
                    visible: enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tb.revealAll()
                }

                // Soft fades at the scroll edges.
                Rectangle {
                    x: chatFlick.x
                    y: chatFlick.y
                    width: chatFlick.width
                    height: 14
                    visible: chatFlick.contentY > 1
                    gradient: Gradient {
                        GradientStop { position: 0; color: tb.buddySurface }
                        GradientStop { position: 1; color: Qt.rgba(tb.buddySurface.r, tb.buddySurface.g, tb.buddySurface.b, 0) }
                    }
                }
                Rectangle {
                    x: chatFlick.x
                    y: chatFlick.y + chatFlick.height - height
                    width: chatFlick.width
                    height: 16
                    visible: chatFlick.contentY < chatFlick.contentHeight - chatFlick.height - 1
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.rgba(tb.buddySurface.r, tb.buddySurface.g, tb.buddySurface.b, 0) }
                        GradientStop { position: 1; color: tb.buddySurface }
                    }
                }

                // Footer: follow-up + copy / again / love / pop out / close.
                Item {
                    x: 10
                    y: 12 + tb.chatHeaderH + tb.chatContentH + 10
                    width: tb.chatW - 20
                    height: tb.footH
                    id: chatFoot
                    // The follow-up field and actions settle in once the words are
                    // all there, instead of showing half-clipped while the card grows.
                    property real ready: tb.revealing ? 0 : 1
                    enabled: ready > 0.5
                    Behavior on ready { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                    opacity: tb.footIn * ready
                    transform: Translate { y: (1 - tb.footIn * chatFoot.ready) * 6 }

                    BuddyAskField {
                        id: followField
                        anchors.left: parent.left
                        anchors.right: chatActions.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        height: 30
                        onSent: (q) => tb.submitted(q)
                        onEscaped: { tb.close(); tb.dismissed() }
                    }
                    Row {
                        id: chatActions
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        BuddyIconButton {
                            icon: tb.copied ? "check" : "copy"
                            tip: qsTr("Copy answer")
                            tint: tb.copied ? Theme.success : Theme.textDim
                            active: tb.copied
                            onClicked: tb.copyAnswer()
                        }
                        BuddyIconButton {
                            icon: "reset"
                            tip: qsTr("Ask again")
                            onClicked: tb.retryRequested()
                        }
                        BuddyIconButton {
                            icon: "heart"
                            tip: qsTr("Thank Buddy")
                            filledIcon: tb.lovedIt
                            active: tb.lovedIt
                            tint: tb.lovedIt ? Theme.love : Theme.textDim
                            onClicked: { tb.lovedIt = true; tb.loved() }
                        }
                        BuddyIconButton {
                            icon: "expand"
                            tip: qsTr("Open in a window")
                            onClicked: tb.expandRequested()
                        }
                        BuddyIconButton {
                            icon: "close"
                            tip: qsTr("Close")
                            tint: Theme.textFaint
                            onClicked: { tb.close(); tb.dismissed() }
                        }
                    }
                }
            }

            // ---- rich cards ----------------------------------------------------
            Item {
                id: cardPane
                width: tb.cardW
                height: parent.height
                x: tb.paneX(width)
                opacity: tb.phase === "answer" && tb.richKind ? 1 : 0
                visible: opacity > 0.01
                enabled: tb.phase === "answer" && tb.richKind
                Behavior on opacity { NumberAnimation { duration: 200 } }

                Item {
                    y: 4 + (1 - tb.contentIn) * 8
                    width: parent.width
                    height: tb.cardBodyH
                    opacity: tb.contentIn

                    Loader {
                        anchors.fill: parent
                        active: tb.phase === "answer" && tb.richKind
                        sourceComponent: tb.kind === "chart" ? chartCard
                                       : tb.kind === "research" ? researchCard
                                       : tb.kind === "tasks" ? tasksCard : helpCard
                    }
                }

                Item {
                    x: 12
                    y: tb.cardBodyH + 8
                    width: parent.width - 22
                    height: tb.footH
                    opacity: tb.footIn
                    transform: Translate { y: (1 - tb.footIn) * 6 }

                    Rectangle {
                        id: sampleChip
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: sampleLabel.implicitWidth + 16
                        height: 22
                        radius: 11
                        color: tb.simulated ? Theme.tile : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
                        border.width: 0
                        border.color: tb.simulated ? Theme.border : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                        Text {
                            id: sampleLabel
                            anchors.centerIn: parent
                            text: tb.simulated ? qsTr("Sample content") : qsTr("Handled by Noch")
                            color: tb.simulated ? Theme.textDim : Theme.accent
                            font.family: Theme.font
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                        }
                    }
                    BuddyAskField {
                        id: cardAsk
                        anchors.left: sampleChip.right
                        anchors.leftMargin: 8
                        anchors.right: cardActions.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        height: 30
                        hint: qsTr("Ask something else…")
                        onSent: (q) => tb.submitted(q)
                        onEscaped: { tb.close(); tb.dismissed() }
                    }
                    Row {
                        id: cardActions
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        BuddyIconButton {
                            icon: "expand"
                            tip: qsTr("Open in a window")
                            onClicked: tb.expandRequested()
                        }
                        BuddyIconButton {
                            icon: "close"
                            tip: qsTr("Close")
                            tint: Theme.textFaint
                            onClicked: { tb.close(); tb.dismissed() }
                        }
                    }
                }
            }

            // ---- quick note (timer set, app opened) -----------------------------
            Item {
                id: notePane
                width: tb.noteW
                height: tb.rowH
                x: tb.paneX(width)
                opacity: tb.phase === "answer" && tb.kind === "note" ? tb.contentIn : 0
                visible: opacity > 0.01

                Rectangle {
                    id: noteBadge
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    radius: 12
                    color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.16)
                    border.width: 0
                    border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.5)
                    scale: 0.6 + 0.4 * tb.contentIn
                    Glyph {
                        anchors.centerIn: parent
                        name: tb.noteIcon
                        size: 13
                        stroke: 2
                        color: Theme.success
                    }
                }
                Text {
                    id: noteLabel
                    x: 46
                    anchors.verticalCenter: parent.verticalCenter
                    text: tb.noteText
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { tb.close(); tb.dismissed() }
                }
            }

            // ---- error ---------------------------------------------------------
            Item {
                id: errorPane
                width: tb.errW
                height: tb.errorH
                x: tb.paneX(width)
                opacity: tb.phase === "error" ? 1 : 0
                visible: opacity > 0.01
                enabled: tb.phase === "error"
                Behavior on opacity { NumberAnimation { duration: 200 } }

                Text {
                    id: errText
                    x: 16
                    y: 16
                    width: parent.width - 32
                    text: tb.errorMessage
                    color: Theme.text
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    wrapMode: Text.WordWrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    lineHeight: 1.1
                }
                Row {
                    x: 12
                    y: errText.y + errText.height + 12
                    spacing: 6

                    Rectangle {
                        width: retryRow.implicitWidth + 20
                        height: 28
                        radius: 14
                        color: retryHit.pressed ? Theme.press : (retryHit.containsMouse ? Theme.tileActive : Theme.tile)
                        border.width: 0
                        border.color: Theme.border
                        Row {
                            id: retryRow
                            anchors.centerIn: parent
                            spacing: 6
                            Glyph { anchors.verticalCenter: parent.verticalCenter; name: "reset"; size: 12; color: Theme.textDim }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Try again")
                                color: Theme.ink
                                font.family: Theme.font
                                font.pixelSize: Theme.fontBase
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            id: retryHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: tb.retryRequested()
                        }
                    }
                    Rectangle {
                        width: 70
                        height: 28
                        radius: 14
                        color: closeHit.pressed ? Theme.press : (closeHit.containsMouse ? Theme.hover : "transparent")
                        border.width: 1
                        border.color: Theme.borderSoft
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Close")
                            color: Theme.textDim
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: closeHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { tb.close(); tb.dismissed() }
                        }
                    }
                }
            }
        }
    }

    Component { id: chartCard; BuddyChartCard { host: tb } }
    Component { id: researchCard; BuddyResearchCard { host: tb } }
    Component { id: tasksCard; BuddyTasksCard { host: tb } }
    Component { id: helpCard; BuddyHelpCard { host: tb } }

    // ---- helpers ------------------------------------------------------------------------------
    property bool copied: false
    property bool lovedIt: false
    onPayloadChanged: { copied = false; lovedIt = false }
    Timer { id: copiedTimer; interval: 1600; onTriggered: tb.copied = false }

    TextEdit { id: clip; visible: false; textFormat: TextEdit.PlainText }
    function copyAnswer() {
        clip.text = fullResponse
        clip.selectAll()
        clip.copy()
        clip.text = ""
        copied = true
        copiedTimer.restart()
        if (BuddySound)
            BuddySound.play("pop")
    }

    function trySubmit() {
        const q = field.text.trim()
        if (q.length === 0)
            return
        submitted(q)
    }
}
