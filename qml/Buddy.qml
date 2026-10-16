import QtQuick
import QtQuick.Shapes

// Noch's desktop buddy: a small creature hanging from a rope at the top edge
// of the screen. It lives inside the existing transparent, always-on-top
// overlay window — no new window, no new platform code — and its hit regions
// are added to the same click-through mask as the notch, its glass to the same
// blur shape list. Drawn with the same Shape/vector pipeline as everything
// else in the notch.
//
// States (one state machine, no overlaps):
//   idle     — swinging, blinking, watching the cursor nearby.
//   input    — clicked: the request bar opens beside it (BuddyInputBar).
//   thinking — a task handler is running; it climbs and hops on the rope.
//   result   — the BuddyPanel (in Main) shows the outcome.
//
// Everything the router calls a demo is labelled as sample data in the
// panels; the timer / open-app / clipboard / mic / palette routes are real
// Noch features, not simulations.
//
// Motion honours Windows' "Animation effects" switch through
// appearance.systemMotionReduced(): with it off, swings get slower and smaller.
Item {
    id: buddy
    objectName: "buddy"

    // ---- wiring from Main ---------------------------------------------------
    property real s: 1                 // follows the notch size scale
    property var desktop: null         // root.contentItem: clamp + mapping space
    property var focusTimer: null      // the real FocusTimer instance
    property var resultPanel: null     // the BuddyPanel instance (a sibling)
    property string savedPlan: ""      // JSON from cfg.buddyPlan (persisted plan)
    property real anchor: 200          // screen x of the rope's top, persisted

    // ---- state ---------------------------------------------------------------
    property string session: "idle"    // idle | input | thinking | result
    property string lastRequest: ""
    readonly property bool inputShown: session === "input" && inputBar.visible
    readonly property Item inputItem: inputBar

    signal buddyMoved(real x)
    signal toastRequested(string text)
    signal paletteRequested()
    signal clipboardRequested()
    signal settingsRequested()
    signal activationRequested()

    Theme { id: t }

    readonly property real mo: appearance.systemMotionReduced() ? 0.35 : 1.0
    readonly property real ropeLen: Math.round(62 * s)
    readonly property real bodyTop: ropeLen - Math.round(14 * s)
    readonly property real bodyH: Math.round(48 * s)
    readonly property real bodyBottom: bodyTop + bodyH

    width: Math.round(92 * s)
    height: bodyBottom + Math.round(16 * s)
    x: Math.round(anchor - width / 2)
    y: 0

    BuddyRouter { id: router }

    // ---- geometry for the click-through mask --------------------------------

    // Interactive rects (buddy + open input bar) in `item` coordinates —
    // Main unions these into the window mask. Swing is covered by margins and
    // never keyed into the mask, so the mask stays static while it swings.
    function regionsIn(item) {
        const out = []
        if (!visible || !desktop)
            return out
        const top = mapToItem(item, width / 2, 0)
        out.push({ x: Math.round(top.x - 14 * s), y: Math.round(top.y),
                   width: Math.round(28 * s), height: Math.round(ropeLen + 10 * s), radius: 10 })
        const hull = mapToItem(item, width / 2 - 26 * s - 8, bodyTop - 8)
        out.push({ x: Math.round(hull.x), y: Math.round(hull.y),
                   width: Math.round(52 * s + 16), height: Math.round(48 * s + 22), radius: 18 })
        if (inputShown) {
            const r = inputBar.mapToItem(item, 0, 0)
            out.push({ x: Math.round(r.x - 6), y: Math.round(r.y - 6),
                       width: Math.round(inputBar.width + 12), height: Math.round(inputBar.height + 12), radius: 22 })
        }
        return out
    }

    // ---- cursor watching ------------------------------------------------------

    // Desktop coordinates of the pointer, smoothed; drives the lean and pupils.
    property real lx: anchor
    property real ly: 0
    Behavior on lx { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
    Behavior on ly { NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }

    readonly property real lean: {
        const dx = lx - anchor
        if (Math.abs(dx) < 6)
            return 0
        return Math.max(-2.6, Math.min(2.6, dx / 44)) * mo
    }

    // Pupil shift inside the fixed 52x48 body space.
    readonly property real eyeShiftX: Math.max(-1.6, Math.min(1.6, (lx - anchor) / 60)) * mo
    readonly property real eyeShiftY: Math.max(-1.4, Math.min(1.6, (ly - (ropeLen + 10 * s)) / 140)) * mo

    // Hover-only probe across the buddy area: acceptedButtons NoButton means it
    // never steals a click; only masked areas deliver events anyway.
    MouseArea {
        id: probe
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        cursorShape: Qt.ArrowCursor
        x: -buddy.x
        y: -buddy.y
        width: buddy.desktop ? buddy.desktop.width : 0
        height: buddy.desktop ? buddy.desktop.height : 0
        onPositionChanged: (mouse) => {
            buddy.lx = mouse.x
            buddy.ly = mouse.y
            // Welcome only when the pointer comes close to the creature
            // itself, not anywhere else the window's mask can see.
            const near = Math.abs(mouse.x - buddy.anchor) < 96 * buddy.s && mouse.y < buddy.bodyBottom + 80 * buddy.s
            if (!near)
                buddy.nearWelcomed = false
            else if (!buddy.nearWelcomed && buddy.session === "idle"
                     && buddy.bubbleText.length === 0) {
                buddy.nearWelcomed = true
                buddy.emote(qsTr("Hi!"))
            }
        }
        onExited: {
            buddy.nearWelcomed = false
            buddy.lx = buddy.anchor
            buddy.ly = buddy.ropeLen
        }
    }

    // ---- blinking ------------------------------------------------------------

    property bool blinkShut: false

    Timer {
        id: blinkLoop
        repeat: true
        running: buddy.visible
        interval: 3400
        onTriggered: {
            interval = 2600 + Math.random() * 3000
            buddy.blinkShut = true
            blinkOpen.restart()
        }
    }
    Timer {
        id: blinkOpen
        interval: 115
        onTriggered: buddy.blinkShut = false
    }

    // ---- idle grip adjustments -------------------------------------------------

    property int gripShift: 0

    Timer {
        id: gripTimer
        repeat: true
        running: buddy.visible && buddy.session === "idle"
        interval: 9000
        onTriggered: {
            interval = 8000 + Math.random() * 6000
            buddy.gripShift = buddy.gripShift === 0 ? 1 : 0
        }
    }

    // ---- rope + rig ------------------------------------------------------------

    Item {
        id: rig
        x: buddy.width / 2
        y: 0
        rotation: 0

        SequentialAnimation on rotation {
            running: buddy.visible
            loops: Animation.Infinite
            NumberAnimation { to: 3.4 * buddy.mo; duration: 2400 / Math.max(0.35, buddy.mo) }
            NumberAnimation { to: -3.4 * buddy.mo; duration: 4800 / Math.max(0.35, buddy.mo) }
            NumberAnimation { to: 0; duration: 2400 / Math.max(0.35, buddy.mo) }
        }

        // Extra body motion: climb hops (thinking) and celebration hops.
        property real bounce: 0
        Behavior on bounce { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        SequentialAnimation on bounce {
            running: buddy.session === "thinking"
            loops: Animation.Infinite
            onRunningChanged: if (!running) rig.bounce = 0
            NumberAnimation { to: -9 * buddy.mo; duration: 300; easing.type: Easing.OutCubic }
            NumberAnimation { to: 0; duration: 260; easing.type: Easing.InOutSine }
            NumberAnimation { to: -5 * buddy.mo; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation { to: 0; duration: 200; easing.type: Easing.InOutSine }
        }

        // ---- rope ----------------------------------------------------------
        Item {
            id: ropeHull
            x: -10
            y: -2
            width: 20
            height: buddy.ropeLen + 10

            readonly property string cord: "M10 0 C8 16 12 34 10 " + Math.round(buddy.ropeLen + 8)

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: t.textDim
                    strokeWidth: 2.2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathSvg { path: ropeHull.cord }
                }
            }

            // Clicking the rope is like clicking the creature.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: if (buddy.session === "input")
                               inputBar.focusInput()
                           else
                               buddy.openInput()
            }
        }

        // ---- body ------------------------------------------------------------
        Item {
            id: bodyHull
            x: -26            // 52-unit space centred on the rope
            y: buddy.bodyTop + rig.bounce
            width: 52
            height: 48
            transformOrigin: Item.Top
            scale: buddy.s * (buddy.session === "input" ? 1.04
                              : (buddy.session === "thinking" ? 1.02 : 1))
            rotation: buddy.session === "thinking" ? 0 : buddy.lean

            Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeWidth: -1
                    fillColor: t.ink
                    PathSvg {
                        path: "M26 0.5 C40.5 0.5 51.5 9.5 51.5 21 C51.5 35 41 47.5 26 47.5 "
                              + "C11 47.5 0.5 35 0.5 21 C0.5 9.5 11.5 0.5 26 0.5 Z"
                    }
                }
            }

            // face: eyes with glints, tiny mouth
            Rectangle {
                id: eyeL
                x: 13.1 + buddy.eyeShiftX
                y: 16.3 + buddy.eyeShiftY
                width: 5
                height: buddy.blinkShut ? 1.2 : (buddy.session === "thinking" ? 10.5 : 9)
                radius: width / 2
                color: t.inverse

                Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 90 } }

                Rectangle {
                    x: 2.1
                    y: 1.5
                    width: 1.7
                    height: 1.7
                    radius: 0.85
                    color: t.light ? Qt.rgba(0, 0, 0, 0.22) : "#ffffff"
                    visible: eyeL.height > 6
                }
            }
            Rectangle {
                id: eyeR
                x: 33.9 + buddy.eyeShiftX
                y: 16.3 + buddy.eyeShiftY
                width: 5
                height: eyeL.height
                radius: width / 2
                color: t.inverse

                Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                Rectangle {
                    x: 1.2
                    y: 1.5
                    width: 1.7
                    height: 1.7
                    radius: 0.85
                    color: t.light ? Qt.rgba(0, 0, 0, 0.22) : "#ffffff"
                    visible: eyeR.height > 6
                }
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: t.inverse
                    strokeWidth: 1.7
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathSvg {
                        path: buddy.session === "thinking"
                              ? "M23.9 30 a2.3 2.3 0 1 0 4.6 0 a2.3 2.3 0 1 0 -4.6 0"
                              : (buddy.celebrated ? "M21.5 28.5 Q26 34 30.5 28.5"
                                                  : "M22.5 29.5 Q26 32.8 29.5 29.5")
                    }
                }
            }

            // click / drag — desktop-coordinate deltas so the maths survives
            // the item moving under the cursor
            MouseArea {
                id: bodyHit
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Noch's desktop buddy")
                Accessible.description: qsTr("Click to ask for a chart demo, a research note or a plan.")

                property bool moved: false
                property real grabX: 0
                property real baseAnchor: 0

                onPressed: (mouse) => {
                    moved = false
                    baseAnchor = buddy.anchor
                    grabX = buddy.desktop ? bodyHit.mapToItem(buddy.desktop, mouse.x, mouse.y).x
                                          : 0
                }
                onPositionChanged: (mouse) => {
                    if (!pressed || !buddy.desktop)
                        return
                    const cur = bodyHit.mapToItem(buddy.desktop, mouse.x, mouse.y).x
                    if (Math.abs(cur - grabX) > 4)
                        moved = true
                    if (moved) {
                        const half = buddy.width / 2
                        buddy.anchor = Math.max(half + 10,
                                                Math.min(buddy.desktop.width - half - 10,
                                                         baseAnchor + (cur - grabX)))
                    }
                }
                onReleased: {
                    if (moved) {
                        buddy.buddyMoved(buddy.anchor)
                    } else if (buddy.session === "input") {
                        inputBar.focusInput()
                    } else {
                        buddy.openInput()
                    }
                }
            }
        }

        // ---- hands: gripping the rope just above the head ----------------------
        property real handSwap: 0

        SequentialAnimation on handSwap {
            running: buddy.session === "thinking"
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: 320 }
            NumberAnimation { to: 0; duration: 320 }
        }

        Rectangle {
            id: handL
            x: -14 * buddy.s
            y: buddy.bodyTop - 10 * buddy.s + buddy.gripShift * 1.8 * buddy.s + rig.bounce * 0.5
            width: 9 * buddy.s
            height: width
            radius: width / 2
            color: t.ink
            Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        }

        Rectangle {
            id: handR
            x: 5 * buddy.s
            y: buddy.bodyTop - 10 * buddy.s - buddy.gripShift * 1.8 * buddy.s
              + rig.handSwap * -5 * buddy.s + rig.bounce * 0.5
            width: 9 * buddy.s
            height: width
            radius: width / 2
            color: t.ink
            transformOrigin: Item.Top

            Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            SequentialAnimation on rotation {
                id: waveAnim
                running: false
                loops: 1
                RotationAnimation { to: 24; duration: 170; easing.type: Easing.OutCubic }
                RotationAnimation { to: -12; duration: 230; easing.type: Easing.InOutSine }
                RotationAnimation { to: 16; duration: 230; easing.type: Easing.InOutSine }
                RotationAnimation { to: -8; duration: 230; easing.type: Easing.InOutSine }
                RotationAnimation { to: 0; duration: 160; easing.type: Easing.OutCubic }
            }
        }

        // ---- feet: dangling nubs with lagged counter-swing ---------------------
        property real swayLag: 0

        SequentialAnimation on swayLag {
            running: buddy.visible
            loops: Animation.Infinite
            NumberAnimation { to: 2.4 * buddy.mo; duration: 2600 }
            NumberAnimation { to: -2.4 * buddy.mo; duration: 5200 }
            NumberAnimation { to: 0; duration: 2600 }
        }

        Rectangle {
            x: -12 * buddy.s
            y: buddy.bodyBottom - 4 * buddy.s + rig.swayLag + rig.bounce
            width: 7 * buddy.s
            height: width
            radius: width / 2
            color: t.ink
        }
        Rectangle {
            x: 5 * buddy.s
            y: buddy.bodyBottom - 4 * buddy.s - rig.swayLag + rig.bounce
            width: 7 * buddy.s
            height: width
            radius: width / 2
            color: t.ink
        }

        // ---- emote bubble -------------------------------------------------------
        Item {
            id: bubble
            visible: opacity > 0.01
            opacity: buddy.bubbleText.length > 0 ? 1 : 0
            x: buddy.inputFlipped ? (-18 * buddy.s - width) : (18 * buddy.s)
            y: buddy.bodyTop - 16 * buddy.s
            width: bubbleLabel.implicitWidth + 20
            height: 26
            Behavior on opacity { NumberAnimation { duration: 140 } }

            Glass {
                anchors.fill: parent
                radius: height / 2
            }
            Text {
                id: bubbleLabel
                anchors.centerIn: parent
                text: buddy.bubbleText
                color: t.text
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }

    // ---- emotes ---------------------------------------------------------------

    property string bubbleText: ""
    property bool nearWelcomed: false
    Timer { id: bubbleTimer; interval: 1700; onTriggered: buddy.bubbleText = "" }

    function emote(text) {
        bubbleText = text
        bubbleTimer.restart()
    }

    function wave() {
        waveAnim.restart()
    }

    property bool celebrated: false
    Timer { id: celebrateCooldown; interval: 1400; onTriggered: buddy.celebrated = false }

    function celebrate() {
        celebrated = true
        celebrateCooldown.restart()
    }

    // ---- request flow ------------------------------------------------------------

    function openInput() {
        activationRequested()
        if (resultPanel)
            resultPanel.open = false
        session = "input"
        inputBar.status = "idle"
        inputBar.statusText = ""
        Qt.callLater(function() { inputBar.focusInput() })
        wave()
    }

    function dismissInput() {
        if (resultPanel && resultPanel.open)
            session = "result"
        else
            session = "idle"
    }

    function dismiss() {
        if (resultPanel)
            resultPanel.open = false
        session = "idle"
    }

    function submit(text) {
        const trimmed = String(text || "").trim()
        if (trimmed.length === 0 || session === "thinking")
            return
        lastRequest = trimmed
        if (resultPanel)
            resultPanel.open = false
        inputBar.status = "thinking"
        inputBar.statusText = ""
        session = "thinking"
        emote(qsTr("On it!"))
        thinkTimer.restart()
    }

    function retryTask() {
        session = "thinking"
        emote(qsTr("On it!"))
        thinkTimer.restart()
    }

    function showError(message) {
        session = "input"
        inputBar.status = "error"
        inputBar.statusText = message
        emote(qsTr("Hmm…"))
    }

    Timer {
        id: thinkTimer
        interval: 620
        onTriggered: buddy.runTask()
    }

    function parsePlan(json) {
        try {
            return JSON.parse(json)
        } catch (e) {
            return null
        }
    }

    // Routes the request. Demo routes (chart / research) say so on the panel;
    // timer / apps / palette / clipboard / mic are real Noch features.
    function runTask() {
        const text = lastRequest
        const task = router.resolve(text)
        if (!task) {
            showError(qsTr("I need a few words to work with."))
            return
        }
        switch (task.kind) {
        case "chart": {
            const chart = router.chartData(task.payload.symbol, text)
            setPanel("chart", task.title, chart, true)
            break
        }
        case "research":
            setPanel("research", task.title, task.payload, true)
            break
        case "tasks": {
            const today = new Date().toISOString().slice(0, 10)
            let rolled = null
            if (savedPlan.length > 0) {
                const parsed = parsePlan(savedPlan)
                if (parsed)
                    rolled = router.planForDay(parsed, today)
            }
            const day = rolled ? rolled.day : today
            const list = rolled ? rolled.items : router.buildPlan(task.payload.templateKey)
            setPanel("tasks", task.title, { items: list, day: day }, true)
            break
        }
        case "timer": {
            if (focusTimer) {
                focusTimer.setMinutes(task.payload.minutes)
                focusTimer.start()
            }
            dismiss()
            emote(qsTr("%1 min").arg(task.payload.minutes))
            toastRequested(qsTr("Focus timer set for %1 minutes").arg(task.payload.minutes))
            break
        }
        case "apps": {
            const rows = search.search(task.payload.query)
                            .filter(function (r) { return r.kind === "app" })
            dismiss()
            if (rows.length > 0) {
                search.activate(rows[0])
                emote(qsTr("Opening…"))
                toastRequested(qsTr("Opened %1").arg(String(rows[0].name || task.payload.query)))
            } else {
                emote(qsTr("No match"))
                toastRequested(qsTr("No app matched “%1”").arg(task.payload.query))
            }
            break
        }
        default:
            setPanel("help", task.title, {}, false)
            break
        }
    }

    function setPanel(kind, title, payload, simulated) {
        if (!resultPanel)
            return
        resultPanel.kind = kind
        resultPanel.title = title
        resultPanel.payload = payload
        resultPanel.simulated = simulated
        resultPanel.phase = "ready"
        if (!resultPanel.open) {
            // First show: under the buddy. Later requests keep the position
            // the user dragged it to.
            resultPanel.dragX = Math.round(anchor - resultPanel.width_ / 2)
            resultPanel.dragY = Math.round(bodyBottom + 16 * s)
            resultPanel.resetBody()
            resultPanel.clampIntoView()
        } else {
            resultPanel.resetBody()
            resultPanel.clampIntoView()
        }
        resultPanel.open = true
        session = "result"
        emote(qsTr("Done!"))
        celebrate()
    }

    // ---- capability actions (the help card) ---------------------------------------

    function runCapability(id) {
        switch (id) {
        case "palette":
            dismiss()
            paletteRequested()
            break
        case "timer":
            if (focusTimer) {
                focusTimer.setMinutes(25)
                focusTimer.start()
            }
            dismiss()
            toastRequested(qsTr("25-minute focus started"))
            break
        case "clipboard":
            dismiss()
            clipboardRequested()
            break
        case "mic":
            audio.toggleMicMute()
            toastRequested(audio.micMuted ? qsTr("Microphone muted") : qsTr("Microphone live"))
            break
        case "settings":
            dismiss()
            settingsRequested()
            break
        }
    }

    // ---- request bar ----------------------------------------------------------------

    // Flip to the left when the buddy sits near the right edge.
    readonly property bool inputFlipped: desktop
        && (desktop.width - (buddy.x + buddy.width) - 12) < inputBar.width + 20

    BuddyInputBar {
        id: inputBar
        objectName: "buddyInputBar"
        visible: buddy.session === "input" || opacity > 0.01
        opacity: buddy.session === "input" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

        // Local coordinates relative to `buddy`, clamped inside desktop bounds.
        x: buddy.inputFlipped
           ? Math.max(8 - buddy.x, -width - 6)
           : Math.min(buddy.desktop ? (buddy.desktop.width - buddy.x - width - 8) : (buddy.width + 6),
                      buddy.width + 6)
        y: Math.round(buddy.bodyTop + (buddy.bodyH - height) / 2)

        onSubmitted: (text) => buddy.submit(text)
        onDismissed: () => buddy.dismissInput()
        onRetryRequested: () => {
            status = "idle"
            statusText = ""
            buddy.submit(buddy.lastRequest)
        }
    }
}
