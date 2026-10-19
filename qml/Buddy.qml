pragma ComponentBehavior: Bound
// Noch's desktop buddy: an expressive companion orb with wide-angle 3D
// sphere-projected eyes, squash-and-stretch physics, mood expressions (happy
// arcs with a neon gradient halo, curious tilt, listening, pondering, reading
// along, speaking pulse), petting detection, procedural sound effects, and a
// thought bubble that carries every request from question to answer.
import QtQuick
import QtQuick.Shapes

Item {
    id: buddy
    objectName: "buddy"

    // ---- wiring from Main ---------------------------------------------------
    property real s: 1
    property var desktop: null
    property var focusTimer: null
    property var resultPanel: null
    property string savedPlan: ""
    property real anchor: 200
    property real anchorY: 8
    property bool motionPaused: false
    property real lastTrackMs: 0
    property int farSide: 0
    property bool solidRing: true

    // ---- state & mood -------------------------------------------------------
    property string session: "idle"      // idle | input | thinking | result
    property string mood: "neutral"      // neutral | happy | love | curious | listening | thinking | speaking | surprised | error | sleepy
    property string lastRequest: ""
    property bool awaitingReply: false   // a chat request is in flight for the bubble
    readonly property bool inputActive: session === "input" || session === "thinking"
    readonly property bool bubbleShown: thought.visible
    readonly property bool dragging: buddyHit.pressed && buddyHit.dragged
    // Eyes stop following the cursor while the buddy ponders or reads its answer.
    readonly property bool gazeLock: session === "thinking" || thought.revealing
    // True for the whole time a reply is being produced: waiting on the model,
    // running a local task, or reading the words out. The orb keeps its neon
    // halo lit meanwhile, so the user can see that work continues even after
    // the bubble has been tucked away by a click somewhere else.
    readonly property bool busy: awaitingReply || thinkTimer.running
                                || (thought.visible && thought.revealing)
                                || (!!resultPanel && resultPanel.open
                                    && (resultPanel.phase === "loading" || resultPanel.isStreaming))
                                || (!!resultPanel && replyToPanel && awaitingReply)

    // The answer last shown. An interrupted one is kept here so clicking the
    // orb continues the same reply instead of starting a blank prompt.
    property string lastAnswerKind: ""
    property string lastAnswerTitle: ""
    property var lastAnswerPayload: null
    property bool lastAnswerSimulated: false
    property real lastAnswerReveal: 1.0
    readonly property bool hasAnswerToContinue: lastAnswerPayload !== null
                                                && lastAnswerReveal < 0.999
    // A popped-out panel holding an answer that is either still being made or
    // only half read out. Closing the panel must not lose that either.
    readonly property bool panelHasMoreToRead: !!resultPanel
        && ((resultPanel.kind === "chat" && resultPanel.phase === "ready"
             && resultPanel.revealedChars < resultPanel.fullResponse.length)
            || (replyToPanel && awaitingReply))
    // True when clicking the orb should reopen work in progress rather than
    // start a fresh prompt.
    readonly property bool canContinue: busy || hasAnswerToContinue || panelHasMoreToRead
    // Set when a reply is requested. A click elsewhere leaves it set so the
    // answer pops back up on its own; an explicit x / Esc clears it, so a
    // deliberately closed bubble stays closed (the answer is still resumable
    // from the orb).
    property bool autoPopPending: true

    // Everything the click-through mask depends on, as one comparable string.
    readonly property bool glowing: neon > 0.02 || busy || sparkAnim.running || burstAnim.running
    readonly property string maskSig: [glowing, emoteChip.visible, Math.round(emoteChip.width),
                                       thought.maskSig].join("|")

    signal buddyMoved(real x, real y)
    signal toastRequested(string text)
    signal paletteRequested()
    signal clipboardRequested()
    signal settingsRequested()
    signal activationRequested()
    signal planEdited(var plan)
    signal historySaved(string json)
    // /mode asks the host to change the saved setting, so Settings stays in step.
    signal permissionModeRequested(string mode)
    // Send what was dictated right away, instead of leaving it in the field to check.
    property bool voiceAutoSend: false

    // ---- history & conversation -------------------------------------------------
    // Every answer is kept (newest first) so it can be reopened later; the
    // conversation is the current bubble's thread, sent along with follow-ups.
    property string historyJson: "[]"
    readonly property var history: {
        try {
            const h = JSON.parse(historyJson)
            return Array.isArray(h) ? h : []
        } catch (e) {
            return []
        }
    }
    readonly property int historyLimit: 60
    property var conversation: []

    function recordHistory(kind, title, payload) {
        if (kind === "note" || lastRequest.length === 0)
            return
        const entry = {
            q: lastRequest,
            kind: kind,
            title: String(title || ""),
            // Long answers stay whole when reopened (was cut at 6000 chars).
            a: kind === "chat" && payload && payload.response ? historyAnswer(payload.response) : "",
            t: Date.now()
        }
        // Asking the same thing again moves it to the top instead of piling up.
        const list = history.filter(function (e) { return !(e.q === entry.q && e.kind === entry.kind) })
        list.unshift(entry)
        historySaved(JSON.stringify(list.slice(0, historyLimit)))
    }
    function clearHistory() {
        historySaved("[]")
    }
    readonly property int historyAnswerChars: 24000
    function historyAnswer(text) {
        const s = String(text || "")
        return s.length <= historyAnswerChars ? s : s.slice(0, historyAnswerChars) + " …"
    }
    function rememberTurn(question, answer) {
        // The turn is saved to the current chat on disk (so it can be resumed
        // after Noch is closed); what goes with the next question comes back
        // from there: the chat's summary, then the newest turns word for word.
        // BuddyAgent shortens very long turns itself, keeping their ending.
        Sessions.appendTurn(question, String(answer))
        conversation = Sessions.context()
    }
    // Reopen a past answer. Chat answers come straight from the record;
    // local cards (chart, research, plan, help) are rebuilt by the router.
    function replayHistory(entry) {
        if (!entry || !entry.q)
            return
        cancelPending()
        lastRequest = entry.q
        replyToPanel = false
        Sessions.newChat()
        conversation = []
        if (entry.kind === "chat") {
            rememberTurn(entry.q, entry.a)
            thought.showAnswer("chat", entry.title || entry.q, { response: entry.a }, false)
            session = "result"
            eureka()
            if (BuddySound)
                BuddySound.play("pop")
        } else {
            session = "thinking"
            runTask()
        }
    }


    readonly property real mo: SystemAppearance.systemMotionReduced() ? 0.35 : 1.0

    // Orb geometry: sized to match the notch height (52px at 1.0x scale).
    readonly property real orbSize: Math.round(52 * s)
    readonly property real orbRadius: orbSize / 2
    width: orbSize
    height: orbSize

    // Drag offsets are applied on top of anchor/anchorY while dragging, then
    // committed via buddyMoved() on release so the bindings from Main.qml
    // are never overwritten.
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    readonly property real baseX: Math.round(anchor - width / 2)
    readonly property real baseY: Math.round(anchorY)
    x: baseX + Math.round(dragOffsetX)
    y: baseY + Math.round(dragOffsetY)

    BuddyRouter { id: router }

    // ---- wide-area 3D sphere model constants --------------------------------
    // Expanded angular range + face-center lead allow the eyes to travel across
    // ~90% of the orb interior with natural 3D foreshortening near the rim.
    readonly property real eyeSplit: 17.5
    readonly property real eyeW: 0.205
    readonly property real eyeH: 0.44
    readonly property real restYaw: 0
    readonly property real restPitch: -4
    readonly property real restRoll: 0
    readonly property real scale_: orbRadius * 0.94
    readonly property real cx_: orbRadius
    readonly property real cy_: orbRadius
    readonly property real yawMax: 58
    readonly property real pitchMax: 50
    readonly property real rollMax: 14

    // ---- gaze & expression physics state ------------------------------------
    property real gazeYaw: restYaw
    property real gazePitch: restPitch
    property real gazeRoll: restRoll
    property real gazeVelYaw: 0
    property real gazeVelPitch: 0
    property real gazeVelRoll: 0

    property real targetYaw: restYaw
    property real targetPitch: restPitch
    property real targetRoll: restRoll

    // Extra face translation lead so eyes sweep across a much larger area
    property real faceLeadX: 0
    property real faceLeadY: 0

    // Dynamic expression modifiers (animated smoothly in gazeSpring)
    property real happyArc: 0          // 0..1: raises lower eyelid into a happy '^' crescent
    property real eyeWide: 0           // -0.3..+0.35: squint vs wide-eyed surprise
    property real asymLeft: 0          // curious asymmetric left eye scale
    property real asymRight: 0         // curious asymmetric right eye scale
    property real speakBounce: 0       // 0..1 rhythmic modulation while speaking/streaming
    property real breathPhase: 0       // continuous organic idle breathing phase
    property real shakeX: 0            // brief horizontal "no" wobble on errors
    property real hopOffsetY: 0        // vertical jump/bounce offset
    property real hopVelY: 0
    property real squashX: 1.0         // squash-and-stretch horizontal scale
    property real squashY: 1.0         // squash-and-stretch vertical scale
    property real dragVelX: 0
    property real dragVelY: 0
    property real neon: 0              // 0..1: neon gradient halo, the buddy's "happy glow"
    // The halo's gradient stops, chosen in Settings > Buddy > Ring colour.
    readonly property var ringColors: SystemAppearance.ringColors

    Timer {
        id: moodResetTimer
        interval: 2600
        onTriggered: {
            if (buddy.session === "thinking")
                buddy.mood = "thinking"
            else if (buddy.session === "input")
                buddy.mood = "listening"
            else
                buddy.mood = "neutral"
            buddy.springing = true
        }
    }

    // Reactions can overlap (a tool error while an approval is pending, a
    // success hop while the user is typing). Higher priority wins; a lower
    // one is ignored until the current reaction expires. Priority 0 = idle.
    //   1 typing/hover  2 tool tick  3 success  4 approval  5 error  6 celebrate
    property int reactPriority: 0
    function react(m, durationMs, priority, strength) {
        const p = priority === undefined ? 1 : priority
        if (p < reactPriority)
            return false
        reactPriority = p
        setMood(m, durationMs || 0)
        if (strength)
            hop(strength)
        if (durationMs && durationMs > 0) {
            reactClearTimer.interval = durationMs
            reactClearTimer.restart()
        } else {
            reactClearTimer.stop()
            // Sticky moods (sleepy, thinking) clear priority immediately so
            // the next event can still interrupt them.
            if (p < 4)
                reactPriority = 0
        }
        return true
    }
    Timer {
        id: reactClearTimer
        onTriggered: buddy.reactPriority = 0
    }

    function setMood(m, durationMs) {
        mood = m
        springing = true
        if (durationMs && durationMs > 0) {
            moodResetTimer.interval = durationMs
            moodResetTimer.restart()
        } else {
            moodResetTimer.stop()
        }
    }

    function hop(strength) {
        hopVelY = -(strength || 110) * mo
        squashX = 0.88
        squashY = 1.14
        springing = true
    }

    // A quick head shake: one cheap property animation, no per-frame JS.
    function shake() {
        if (mo < 0.5)
            return
        shakeAnim.restart()
    }
    SequentialAnimation {
        id: shakeAnim
        NumberAnimation { target: buddy; property: "shakeX"; to: -4 * buddy.s; duration: 55; easing.type: Easing.OutQuad }
        NumberAnimation { target: buddy; property: "shakeX"; to: 4 * buddy.s; duration: 90; easing.type: Easing.InOutQuad }
        NumberAnimation { target: buddy; property: "shakeX"; to: -2.5 * buddy.s; duration: 80; easing.type: Easing.InOutQuad }
        NumberAnimation { target: buddy; property: "shakeX"; to: 0; duration: 70; easing.type: Easing.OutQuad }
    }

    // Small success beat when an agent run finishes cleanly with tools.
    function nodHappy() {
        if (!react("happy", 1500, 3, 70))
            return
        neonBurst(false)
    }

    // ---- idle → sleepy --------------------------------------------------------
    // After a few quiet minutes the buddy dozes (droopy eyes, an occasional
    // zzz). The timer only runs while idle, so it costs nothing otherwise, and
    // the sleepy face settles the frame loop like any other expression.
    Timer {
        id: dozeTimer
        interval: 180000
        running: buddy.visible && buddy.session === "idle" && buddy.mood === "neutral"
                 && !buddy.busy && !buddy.dragging
        onTriggered: {
            buddy.setMood("sleepy", 0)
            buddy.targetPitch = -8 * buddy.mo
            buddy.emote("", "zzz")
        }
    }
    Timer {
        interval: 14000
        repeat: true
        running: buddy.visible && buddy.mood === "sleepy"
        onTriggered: buddy.emote("", "zzz")
    }
    function wake() {
        if (mood !== "sleepy")
            return
        targetPitch = restPitch
        setMood("surprised", 650)
        hop(55)
    }
    onSessionChanged: { wake(); dozeTimer.restart() }

    function celebrate() {
        if (!react("love", 3000, 6, 145))
            return
        targetRoll = 12 * mo
        neonBurst(true)
        if (!thought.visible)
            emote("", "heart")
        if (BuddySound)
            BuddySound.play("happy")
    }

    // A ripple of the neon ring, optionally with a little ring of sparkles.
    function neonBurst(sparkles) {
        burstAnim.restart()
        if (sparkles)
            sparkAnim.restart()
        springing = true
    }

    // "Got it": a squash-and-blink gulp when a request is handed over.
    function gulp() {
        squashX = 1.14
        squashY = 0.86
        hopVelY = -60 * mo
        blinkShut = true
        blinkOpen.restart()
        springing = true
    }

    // "Aha!": wide eyes for a beat, then a happy hop with the neon ripple.
    function eureka() {
        setMood("surprised", 0)
        targetPitch = 8 * mo
        springing = true
        eurekaTimer.restart()
    }
    Timer {
        id: eurekaTimer
        interval: 230
        onTriggered: {
            buddy.setMood("happy", 2400)
            buddy.hop(118)
            buddy.neonBurst(true)
            buddy.lookAtBubble(26, -4)
        }
    }

    // Turn the eyes toward the thought bubble (yaw in degrees away from the
    // orb on the bubble's side, pitch > 0 looks up).
    readonly property real bubbleSide: bubbleFlipped ? -1 : 1
    function lookAtBubble(yaw, pitch) {
        if (bubbleCentered) {
            const vSign = bubbleAbove ? 1 : -1
            targetYaw = ((Math.random() - 0.5) * 8) * mo
            targetPitch = Math.max(-pitchMax, Math.min(pitchMax, vSign * 28 + pitch * 0.4)) * mo
            targetRoll = 0
        } else {
            const diagPitch = (bubbleAbove ? 18 : -18) + pitch * 0.5
            targetYaw = bubbleSide * Math.min(yawMax, yaw) * mo
            targetPitch = Math.max(-pitchMax, Math.min(pitchMax, diagPitch)) * mo
            targetRoll = -bubbleSide * 5 * mo
        }
        glanceAt = wanderT + 1.6
        springing = true
    }

    // Reading its own answer as it appears: eyes on the bubble, drifting down
    // the lines with a soft speaking bounce.
    function readAlong(progress) {
        pulseSpeaking()
        const sway = Math.sin(wanderT * 9) * 4
        lookAtBubble(24 + sway, 6 - 24 * Math.min(1, progress))
    }

    // The answer has finished revealing: glance back at the user, pleased.
    function answerSettled() {
        rememberAnswer()
        // Fully read: nothing left to "continue", so the next click prompts.
        if (!hasAnswerToContinue)
            forgetAnswer()
        setMood("happy", 1800)
        hop(60)
        const c = Overlay.cursorPos()
        lx = c.x
        ly = c.y
        cursorStill = 0
        updateGaze()
        springing = true
    }

    function pulseSpeaking() {
        if (mood !== "speaking" && mood !== "love")
            setMood("speaking", 900)
        else if (mood === "speaking")
            moodResetTimer.restart()
        speakBounce = 0.55 + Math.random() * 0.45
        springing = true
    }

    // ---- natural blink (with occasional human double-blink) -----------------
    property bool blinkShut: false
    property bool pendingDoubleBlink: false
    Timer {
        id: blinkLoop
        repeat: true
        running: buddy.visible && !buddy.motionPaused
        interval: 3200
        onTriggered: {
            interval = 2200 + Math.random() * 3200
            buddy.pendingDoubleBlink = Math.random() < 0.28
            buddy.blinkShut = true
            blinkOpen.restart()
        }
    }
    Timer {
        id: blinkOpen
        interval: 105
        onTriggered: {
            buddy.blinkShut = false
            if (buddy.pendingDoubleBlink) {
                buddy.pendingDoubleBlink = false
                doubleBlinkGap.restart()
            }
        }
    }
    Timer {
        id: doubleBlinkGap
        interval: 95
        onTriggered: {
            buddy.blinkShut = true
            blinkOpen.restart()
        }
    }

    // ---- cursor tracking -----------------------------------------------------
    property real lx: anchor
    property real ly: anchorY + orbRadius

    // ---- geometry for the click-through mask --------------------------------
    function regionsIn(item) {
        const out = []
        if (!visible || !desktop)
            return out
        const origin = mapToItem(item, 0, 0)
        // Room for the neon glow and sparkles only while they are showing,
        // so the orb doesn't swallow clicks around it the rest of the time.
        const pad = glowing ? Math.round(24 * s) : 6
        out.push({ x: Math.round(origin.x - pad), y: Math.round(origin.y - pad),
                   width: Math.round(width + 2 * pad), height: Math.round(height + 2 * pad),
                   radius: Math.round(Math.min(width, height) / 2 + pad) })
        if (emoteChip.visible) {
            const e = emoteChip.mapToItem(item, 0, 0)
            out.push({ x: Math.round(e.x - 4), y: Math.round(e.y - 4),
                       width: Math.round(emoteChip.width + 8), height: Math.round(emoteChip.height + 8),
                       radius: 16 })
        }
        if (thought.visible) {
            const b = thought.maskBody
            const tl = thought.maskTail
            out.push({ x: Math.round(origin.x + b.x), y: Math.round(origin.y + b.y),
                       width: b.width, height: b.height, radius: 28 })
            out.push({ x: Math.round(origin.x + tl.x), y: Math.round(origin.y + tl.y),
                       width: tl.width, height: tl.height, radius: 10 })
        }
        return out
    }

    // ---- gaze update (wide-area 3D sphere model) ----------------------------
    function deg(d) { return d * Math.PI / 180 }

    function spin(u, v, angle) {
        const c = Math.cos(angle)
        const s = Math.sin(angle)
        return [
            [u[0]*c + v[0]*s, u[1]*c + v[1]*s, u[2]*c + v[2]*s],
            [v[0]*c - u[0]*s, v[1]*c - u[1]*s, v[2]*c - u[2]*s]
        ]
    }

    function computeEyePoses() {
        let f = [0, 0, 1], right = [1, 0, 0], down = [0, 1, 0]
        const yaw = buddy.gazeYaw
        const pitch = buddy.gazePitch
        const roll = buddy.gazeRoll
        ;[f, right] = spin(f, right, deg(yaw))
        ;[down, f]  = spin(down, f, deg(pitch))
        ;[right, down] = spin(right, down, deg(roll))

        const leadX = (yaw / buddy.yawMax) * (buddy.orbRadius * 0.18)
        const leadY = -(pitch / buddy.pitchMax) * (buddy.orbRadius * 0.16)
        buddy.faceLeadX = leadX
        buddy.faceLeadY = leadY

        const build = (side) => {
            const [ef, er] = spin(f, right, deg(buddy.eyeSplit * side))
            const tilt = (yaw - buddy.restYaw) * 0.30
            const sideTilt = (side < 0)
                ? (yaw < buddy.restYaw ? tilt : tilt * 0.28)
                : (yaw > buddy.restYaw ? tilt : tilt * 0.28)
            const phi = deg(sideTilt)
            const cp = Math.cos(phi), sp = Math.sin(phi)

            const asym = side < 0 ? buddy.asymLeft : buddy.asymRight
            const wScale = Math.max(0.65, 1.0 + buddy.eyeWide * 0.45 + buddy.happyArc * 0.12)
            const hBase = Math.max(0.35, 1.0 + buddy.eyeWide + asym + buddy.speakBounce * 0.28 - buddy.happyArc * 0.18)
            const k = buddy.blinkShut ? 0.08 : hBase

            const ax = (er[0] * cp + down[0] * sp) * wScale
            const ay = (er[1] * cp + down[1] * sp) * wScale
            const cx_2 = (-er[0] * sp + down[0] * cp) * k
            const cy_2 = (-er[1] * sp + down[1] * cp) * k

            return {
                x: ef[0] * buddy.scale_ + leadX,
                y: ef[1] * buddy.scale_ + leadY,
                a: ax, b: ay,
                c: cx_2, d: cy_2,
                depth: ef[2]
            }
        }
        const L = build(-1)
        const R = build(1)

        eyeLMat.matrix = Qt.matrix4x4(L.a, L.c, 0, buddy.cx_ + L.x,
                                      L.b, L.d, 0, buddy.cy_ + L.y,
                                      0, 0, 1, 0,
                                      0, 0, 0, 1)
        eyeL.visible = L.depth > -0.25
        eyeL.opacity = Math.max(0, Math.min(1, (L.depth + 0.22) / 0.28))

        eyeRMat.matrix = Qt.matrix4x4(R.a, R.c, 0, buddy.cx_ + R.x,
                                      R.b, R.d, 0, buddy.cy_ + R.y,
                                      0, 0, 1, 0,
                                      0, 0, 0, 1)
        eyeR.visible = R.depth > -0.25
        eyeR.opacity = Math.max(0, Math.min(1, (R.depth + 0.22) / 0.28))

    }

    // Soft non-linear response curve: high sensitivity near the buddy so small
    // cursor movements feel immediately alive, while reaching smoothly all the
    // way to the edges of the monitor.
    function softCurve(delta, nearRef, farRef) {
        const sign = delta < 0 ? -1 : 1
        const ad = Math.abs(delta)
        const nearPart = 1.0 - Math.exp(-ad / nearRef)
        const farPart = Math.min(1.0, ad / farRef)
        return sign * Math.min(1.0, nearPart * 0.72 + farPart * 0.28)
    }

    function updateGaze() {
        if (!desktop)
            return
        const bx = buddy.x + buddy.width / 2
        const by = buddy.y + buddy.height / 2
        const dx = lx - bx
        const dy = ly - by

        const farX = Math.max(360, desktop.width * 0.55)
        const farY = Math.max(280, desktop.height * 0.55)
        let nx = softCurve(dx, 140, farX)
        let ny = softCurve(dy, 120, farY)

        // Radial clamp so diagonal corner gaze stays naturally on the sphere
        const m = Math.hypot(nx, ny)
        if (m > 1.08) {
            nx = (nx / m) * 1.08
            ny = (ny / m) * 1.08
        }

        buddy.targetYaw = nx * buddy.yawMax * mo
        // Negative pitch in spin(down, f, pitch) points downward (+y on screen).
        buddy.targetPitch = -ny * buddy.pitchMax * mo
        buddy.targetRoll = -nx * buddy.rollMax * 0.65 * mo
    }

    // ---- VSync spring animation + high-rate cursor poll ---------------------
    property real wanderT: 0
    property int cursorStill: 0
    property bool springing: true
    property real glanceAt: 1.4

    // Petting detector (rapid horizontal/vertical strokes across the orb)
    property real petAccum: 0
    property real lastHoverX: 0
    property real lastHoverY: 0

    function scheduleGlance() {
        const t = buddy.wanderT + 1.2 + Math.random() * 1.8
        buddy.wanderT = t
        if (buddy.session === "thinking") {
            // Pondering: slow upward glances that drift toward the thought
            // bubble, now and then a little sideways "hmm" tilt.
            const toBubble = Math.random() < 0.7
            buddy.targetYaw   = (toBubble ? buddy.bubbleSide * (8 + Math.random() * 28)
                                          : (Math.random() - 0.5) * 30) * mo
            buddy.targetPitch = (14 + Math.random() * 22) * mo
            buddy.targetRoll  = (Math.random() - 0.5) * 16 * mo
            buddy.glanceAt = t + 1.0 + Math.random() * 1.1
            return
        }
        if (buddy.session === "input") {
            // Attentive: mostly watching the bubble, sometimes back at you.
            if (Math.random() < 0.7) {
                buddy.lookAtBubble(20 + Math.random() * 12, -2 + Math.random() * 8)
            } else {
                buddy.targetYaw = buddy.bubbleSide * 4 * mo
                buddy.targetPitch = -6 * mo
                buddy.targetRoll = 0
            }
            buddy.glanceAt = t + 1.4 + Math.random() * 1.6
            return
        }
        // Natural idle curiosity: mix wide glances with tiny micro-saccades
        const wideGlance = Math.random() < 0.45
        const amp = wideGlance ? 1.0 : 0.35
        buddy.targetYaw   = buddy.restYaw + (24 * Math.sin(t * 0.55) + 14 * Math.cos(t * 0.9)) * amp * mo
        buddy.targetPitch = buddy.restPitch + (16 * Math.sin(t * 0.42) - 10 * Math.cos(t * 0.8)) * amp * mo
        buddy.targetRoll  = buddy.restRoll + 6.0 * Math.sin(t * 0.35) * amp * mo
        if (wideGlance && buddy.mood === "neutral" && Math.random() < 0.35)
            buddy.setMood("curious", 1400)
        buddy.glanceAt = t + (wideGlance ? (1.6 + Math.random() * 2.0) : (0.6 + Math.random() * 0.9))
    }

    // Fast 20ms poll checks cursor movement so tracking wakes up with zero lag.
    Timer {
        id: cursorPollTimer
        repeat: true
        running: buddy.visible && !buddy.motionPaused
        interval: 20
        onTriggered: {
            buddy.wanderT += 0.020
            buddy.petAccum = Math.max(0, buddy.petAccum - 3.5)

            const c = Overlay.cursorPos()
            const moved = Math.abs(c.x - buddy.lx) + Math.abs(c.y - buddy.ly) > 1.2
            if (moved && !buddy.gazeLock) {
                buddy.lx = c.x
                buddy.ly = c.y
                buddy.cursorStill = 0
                buddy.updateGaze()
                buddy.springing = true
            } else {
                buddy.cursorStill = Math.min(1000, buddy.cursorStill + 1)
                if ((buddy.cursorStill >= 35 || buddy.session === "thinking")
                        && !thought.revealing && buddy.wanderT >= buddy.glanceAt) {
                    buddy.scheduleGlance()
                    buddy.springing = true
                }
            }
        }
    }

    // VSync-locked FrameAnimation runs while eyes or expressions are active.
    FrameAnimation {
        id: gazeSpring
        running: buddy.visible && !buddy.motionPaused
                 && (buddy.springing || buddy.session !== "idle" || buddy.busy
                     || buddy.mood !== "neutral" || buddyHit.containsMouse)
        onTriggered: {
            const dt = Math.min(0.033, Math.max(0.004, frameTime))
            buddy.breathPhase += dt * (buddy.session === "thinking" ? 5.2 : 2.4)

            // Live per-frame cursor tracking while cursor is moving
            if (buddy.cursorStill < 20 && !buddy.gazeLock && !buddy.dragging) {
                const c = Overlay.cursorPos()
                if (Math.abs(c.x - buddy.lx) + Math.abs(c.y - buddy.ly) > 0.4) {
                    buddy.lx = c.x
                    buddy.ly = c.y
                    buddy.cursorStill = 0
                    buddy.updateGaze()
                }
            }

            // Target expression morphs based on current mood & interaction
            const wantHappy = (buddy.mood === "happy" || buddy.mood === "love"
                               || (buddyHit.containsMouse && !buddy.dragging && buddy.session === "idle")) ? 1.0 : 0.0
            const wantWide = buddy.dragging ? 0.30
                           : (buddy.mood === "surprised" ? 0.34
                           : (buddy.mood === "listening" ? 0.14
                           : (buddy.mood === "thinking" ? -0.06
                           : (buddy.mood === "error" ? -0.22
                           : (buddy.mood === "sleepy" ? -0.5 : 0.0)))))
            // Curious: one eye a touch bigger. Pondering: a slight one-eyed squint.
            const wantAsymL = buddy.mood === "curious" ? 0.18 : (buddy.mood === "thinking" ? 0.05 : 0.0)
            const wantAsymR = buddy.mood === "curious" ? -0.14 : (buddy.mood === "thinking" ? -0.13 : 0.0)
            const wantNeon = buddy.busy ? 1.0
                           : (buddy.mood === "happy" || buddy.mood === "love") ? 1.0
                           : (buddyHit.containsMouse && !buddy.dragging && buddy.session === "idle" ? 0.55 : 0.0)

            const expRate = Math.min(1.0, dt * 14.0)
            buddy.happyArc  += (wantHappy - buddy.happyArc) * expRate
            buddy.eyeWide   += (wantWide  - buddy.eyeWide)  * expRate
            buddy.asymLeft  += (wantAsymL - buddy.asymLeft) * expRate
            buddy.asymRight += (wantAsymR - buddy.asymRight)* expRate
            buddy.speakBounce = Math.max(0, buddy.speakBounce - dt * 4.2)
            buddy.neon += (wantNeon - buddy.neon) * Math.min(1.0, dt * (wantNeon > buddy.neon ? 9.0 : 4.5))
            if (Math.abs(wantNeon - buddy.neon) < 0.004)
                buddy.neon = wantNeon

            // Hop & squash-and-stretch spring physics
            const hopK = 280, hopD = 20
            buddy.hopVelY += (-buddy.hopOffsetY * hopK - buddy.hopVelY * hopD) * dt
            buddy.hopOffsetY += buddy.hopVelY * dt
            buddy.squashX += (1.0 - buddy.squashX) * Math.min(1.0, dt * 16.0)
            buddy.squashY += (1.0 - buddy.squashY) * Math.min(1.0, dt * 16.0)
            buddy.dragVelX *= Math.pow(0.08, dt)
            buddy.dragVelY *= Math.pow(0.08, dt)

            // Eye gaze spring physics (snappy, responsive, critically damped)
            const K = 260, D = 28
            buddy.gazeVelYaw   += ((buddy.targetYaw   - buddy.gazeYaw)   * K - buddy.gazeVelYaw   * D) * dt
            buddy.gazeVelPitch += ((buddy.targetPitch - buddy.gazePitch) * K - buddy.gazeVelPitch * D) * dt
            buddy.gazeVelRoll  += ((buddy.targetRoll  - buddy.gazeRoll)  * K - buddy.gazeVelRoll  * D) * dt
            buddy.gazeYaw   += buddy.gazeVelYaw   * dt
            buddy.gazePitch += buddy.gazeVelPitch * dt
            buddy.gazeRoll  += buddy.gazeVelRoll  * dt

            const settled = Math.abs(buddy.gazeVelYaw) + Math.abs(buddy.gazeVelPitch) + Math.abs(buddy.gazeVelRoll) < 0.04
                && Math.abs(buddy.targetYaw - buddy.gazeYaw) < 0.04
                && Math.abs(buddy.targetPitch - buddy.gazePitch) < 0.04
                && Math.abs(buddy.hopOffsetY) < 0.08
                && Math.abs(buddy.hopVelY) < 0.5
                && Math.abs(wantHappy - buddy.happyArc) < 0.01
                && Math.abs(wantWide - buddy.eyeWide) < 0.01
                && buddy.neon === wantNeon
                && buddy.speakBounce <= 0.01
            if (settled) {
                buddy.gazeVelYaw = 0
                buddy.gazeVelPitch = 0
                buddy.gazeVelRoll = 0
                buddy.gazeYaw = buddy.targetYaw
                buddy.gazePitch = buddy.targetPitch
                buddy.gazeRoll = buddy.targetRoll
                buddy.hopOffsetY = 0
                buddy.hopVelY = 0
            }
            buddy.computeEyePoses()
            if (settled)
                buddy.springing = false
        }
    }

    onBlinkShutChanged: computeEyePoses()
    onSChanged: computeEyePoses()

    // ---- ambient aura / thinking & speaking halo ----------------------------
    Rectangle {
        id: auraRing
        anchors.centerIn: orb
        width: orb.width + Math.round(10 * buddy.s) + Math.round(buddy.speakBounce * 8 * buddy.s)
        height: width
        radius: width / 2
        color: "transparent"
        border.width: buddy.solidRing && (buddy.session === "thinking" || buddy.busy
                                          || buddy.mood === "speaking") ? 2 : 0
        border.color: Theme.accent
        opacity: (buddy.session === "thinking" || buddy.busy)
                 ? (0.45 + 0.35 * Math.sin(buddy.breathPhase * 2.0))
                 : (buddy.mood === "speaking" ? 0.55 * (1 - buddy.neon) : 0)
        visible: buddy.solidRing && opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }

    // ---- visual: expressive companion orb -----------------------------------
    Rectangle {
        id: orb
        width: parent.width
        height: parent.height
        // Subtle body lean toward gaze + organic breathing bob + hop bounce
        x: Math.round(buddy.faceLeadX * 0.28 + buddy.shakeX)
        y: Math.round(buddy.faceLeadY * 0.25 + buddy.hopOffsetY
                      + (buddy.mo > 0.5 ? Math.sin(buddy.breathPhase) * 1.4 * buddy.s : 0))
        radius: buddy.orbRadius
        color: Theme.orb
        border.width: !buddy.solidRing ? 0
                    : (buddyHit.containsMouse || buddy.session !== "idle" || buddy.mood === "love" ? 2 : 1)
        border.color: !buddy.solidRing ? "transparent"
                    : (buddy.mood === "error" ? Theme.danger
                    : (buddy.session === "thinking" || buddy.busy || buddy.mood === "speaking" ? Theme.accent
                    : (buddyHit.containsMouse || buddy.session === "input" ? Theme.accent : Theme.borderStrong)))
        Behavior on border.color { ColorAnimation { duration: 140 } }

        rotation: Math.max(-14, Math.min(14, buddy.gazeRoll * 0.55 + buddy.dragVelX * 0.18))

        transform: Scale {
            origin.x: orb.width / 2
            origin.y: orb.height / 2
            xScale: (buddyHit.pressed && !buddyHit.dragged ? 0.92 : 1.0) * buddy.squashX
            yScale: (buddyHit.pressed && !buddyHit.dragged ? 0.92 : 1.0) * buddy.squashY
        }

        // Subtle inner top-rim highlight so the sphere feels tactile and 3D
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(3 * buddy.s)
            width: Math.round(parent.width * 0.54)
            height: Math.round(parent.height * 0.22)
            radius: height / 2
            color: "#ffffff"
            opacity: 0.055
        }

        // left eye
        Item {
            id: eyeL
            x: -(buddy.eyeW * buddy.scale_) / 2
            y: -(buddy.eyeH * buddy.scale_) / 2
            width: buddy.eyeW * buddy.scale_
            height: buddy.eyeH * buddy.scale_
            transform: Matrix4x4 { id: eyeLMat }

            Rectangle {
                id: eyeLPill
                anchors.fill: parent
                radius: width / 2
                color: buddy.mood === "love" ? Theme.loveSoft
                     : (buddy.mood === "error" ? Theme.danger
                     : ((buddy.session === "thinking" || buddy.busy) ? Theme.accent : "#ffffff"))
                Behavior on color { ColorAnimation { duration: 140 } }
                antialiasing: true
            }

            // Lower-eyelid mask that rises up when happy to carve a cute '^' arc
            Rectangle {
                width: parent.width * 1.45
                height: parent.height * 0.95
                radius: width / 2
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * (1.05 - buddy.happyArc * 0.62)
                color: orb.color
                visible: buddy.happyArc > 0.03 && !buddy.blinkShut
                antialiasing: true
            }

            // Inner specular glint dot (gives soul & lifelike depth)
            Rectangle {
                width: Math.max(2, Math.round(parent.width * 0.34))
                height: width
                radius: width / 2
                x: Math.round(parent.width * 0.22)
                y: Math.round(parent.height * 0.16)
                color: "#ffffff"
                opacity: buddy.blinkShut ? 0 : 0.92
            }
        }

        // right eye
        Item {
            id: eyeR
            x: -(buddy.eyeW * buddy.scale_) / 2
            y: -(buddy.eyeH * buddy.scale_) / 2
            width: buddy.eyeW * buddy.scale_
            height: buddy.eyeH * buddy.scale_
            transform: Matrix4x4 { id: eyeRMat }

            Rectangle {
                id: eyeRPill
                anchors.fill: parent
                radius: width / 2
                color: buddy.mood === "love" ? Theme.loveSoft
                     : (buddy.mood === "error" ? Theme.danger
                     : ((buddy.session === "thinking" || buddy.busy) ? Theme.accent : "#ffffff"))
                Behavior on color { ColorAnimation { duration: 140 } }
                antialiasing: true
            }

            // Lower-eyelid mask that rises up when happy to carve a cute '^' arc
            Rectangle {
                width: parent.width * 1.45
                height: parent.height * 0.95
                radius: width / 2
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * (1.05 - buddy.happyArc * 0.62)
                color: orb.color
                visible: buddy.happyArc > 0.03 && !buddy.blinkShut
                antialiasing: true
            }

            // Inner specular glint dot
            Rectangle {
                width: Math.max(2, Math.round(parent.width * 0.34))
                height: width
                radius: width / 2
                x: Math.round(parent.width * 0.22)
                y: Math.round(parent.height * 0.16)
                color: "#ffffff"
                opacity: buddy.blinkShut ? 0 : 0.92
            }
        }
    }

    // ---- neon happiness halo -------------------------------------------------
    // When the buddy is happy a ring of light blooms around the orb: a crisp
    // conical-gradient stroke wrapped in wider, fainter copies of itself that
    // read as a soft neon glow, slowly turning so the colours flow around the
    // rim. (Replaces the old pink cheek blush.)
    component NeonRing: Shape {
        id: ring
        property real midR: 30          // centre line of the stroke
        property real thickness: 2
        property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
        readonly property real outerR: midR + thickness / 2
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: -1
            fillRule: ShapePath.OddEvenFill
            fillGradient: ConicalGradient {
                centerX: ring.width / 2
                centerY: ring.height / 2
                angle: 90
                GradientStop { position: 0.00; color: ring.stops[0] }
                GradientStop { position: 0.27; color: ring.stops[1] }
                GradientStop { position: 0.52; color: ring.stops[2] }
                GradientStop { position: 0.76; color: ring.stops[3] }
                GradientStop { position: 1.00; color: ring.stops[0] }
            }
            PathAngleArc {
                centerX: ring.width / 2
                centerY: ring.height / 2
                radiusX: ring.outerR
                radiusY: ring.outerR
                startAngle: 0
                sweepAngle: 360
            }
            PathAngleArc {
                moveToStart: true
                centerX: ring.width / 2
                centerY: ring.height / 2
                radiusX: ring.outerR - ring.thickness
                radiusY: ring.outerR - ring.thickness
                startAngle: 0
                sweepAngle: 360
            }
        }
    }

    Item {
        id: neonHalo
        readonly property real pad: Math.round(8 * buddy.s)
        x: orb.x - pad
        y: orb.y - pad
        width: orb.width + 2 * pad
        height: width
        // While a reply is in flight the halo stays lit and breathes gently,
        // so the orb reads as "still working" even with the bubble tucked away.
        opacity: buddy.busy ? buddy.neon * (0.80 + 0.20 * Math.sin(buddy.breathPhase * 1.7))
                            : buddy.neon
        visible: opacity > 0.01
        // Squash and press with the orb so the ring always hugs its rim.
        transform: Scale {
            origin.x: neonHalo.width / 2
            origin.y: neonHalo.height / 2
            xScale: (buddyHit.pressed && !buddyHit.dragged ? 0.92 : 1.0) * buddy.squashX
            yScale: (buddyHit.pressed && !buddyHit.dragged ? 0.92 : 1.0) * buddy.squashY
        }

        Item {
            anchors.fill: parent
            // Turning the finished rings is only a transform; their geometry
            // is never rebuilt while they spin.
            NumberAnimation on rotation {
                from: 0
                to: 360
                duration: buddy.mo < 1 ? 9000 : 3600
                loops: Animation.Infinite
                running: neonHalo.visible && !buddy.motionPaused
            }

            // Glow: progressively wider, fainter strokes on the same centre line.
            Repeater {
                model: [{ w: 15, o: 0.06 }, { w: 9.5, o: 0.12 }, { w: 5.5, o: 0.26 }]
                NeonRing {
                    required property var modelData
                    anchors.fill: parent
                    midR: buddy.orbRadius + 0.6 * buddy.s
                    thickness: modelData.w * buddy.s
                    stops: buddy.ringColors
                    opacity: modelData.o
                }
            }
            NeonRing {
                anchors.fill: parent
                midR: buddy.orbRadius + 0.6 * buddy.s
                thickness: Math.max(1.6, 2.4 * buddy.s)
                stops: buddy.ringColors
            }
        }
    }

    // One ripple of the ring travelling outward (answer arrived, petted, set down).
    property real burstT: 0
    NumberAnimation {
        id: burstAnim
        target: buddy
        property: "burstT"
        from: 0
        to: 1
        duration: 640
        easing.type: Easing.OutCubic
    }
    NeonRing {
        width: orb.width + Math.round(16 * buddy.s)
        height: width
        x: orb.x + orb.width / 2 - width / 2
        y: orb.y + orb.height / 2 - height / 2
        midR: buddy.orbRadius + 0.6 * buddy.s
        thickness: Math.max(1.2, 1.8 * buddy.s)
        stops: buddy.ringColors
        visible: burstAnim.running
        opacity: (1 - buddy.burstT) * 0.85
        scale: 1 + 0.38 * buddy.burstT * buddy.mo
        rotation: buddy.burstT * 120
    }

    // A little ring of sparkles that pops and drifts outward.
    property real sparkT: 0
    NumberAnimation {
        id: sparkAnim
        target: buddy
        property: "sparkT"
        from: 0
        to: 1
        duration: 950
        easing.type: Easing.OutCubic
    }
    Repeater {
        model: buddy.ringColors.concat([buddy.ringColors[1]])

        Glyph {
            required property var modelData
            required property int index
            readonly property real ang: (-90 + index * 72 + 14) * Math.PI / 180
            readonly property real rr: buddy.orbRadius + (5 + 11 * buddy.sparkT * buddy.mo) * buddy.s
            name: "spark"
            size: Math.round((index % 2 ? 7 : 9) * buddy.s)
            color: modelData
            x: orb.x + buddy.orbRadius + Math.cos(ang) * rr - width / 2
            y: orb.y + buddy.orbRadius + Math.sin(ang) * rr - height / 2
            visible: sparkAnim.running
            opacity: Math.sin(Math.min(1, buddy.sparkT) * Math.PI)
            scale: 0.3 + 0.9 * Math.sin(Math.min(1, buddy.sparkT * 1.4) * Math.PI / 2)
            rotation: buddy.sparkT * 70 * (index % 2 ? -1 : 1)
        }
    }

    // ---- emote chip (a heart when petted) -------------------------------------
    Item {
        id: emoteChip
        visible: opacity > 0.01
        opacity: (buddy.bubbleText.length > 0 || buddy.emoteIcon.length > 0) && !thought.visible ? 1 : 0
        x: Math.round((buddy.width - width) / 2)
        y: buddy.y > height + 12 ? -height - 6 : buddy.height + 6
        width: (buddy.emoteIcon.length > 0 ? 14 : 0)
               + (buddy.bubbleText.length > 0 ? emoteLabel.implicitWidth : 0)
               + (buddy.emoteIcon.length > 0 && buddy.bubbleText.length > 0 ? 5 : 0) + 18
        height: 24
        Behavior on opacity { NumberAnimation { duration: 140 } }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderStrong
        }
        Row {
            anchors.centerIn: parent
            spacing: 5
            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                visible: buddy.emoteIcon.length > 0
                name: buddy.emoteIcon
                filled: true
                size: 13
                color: buddy.ringColors[2]
            }
            Text {
                id: emoteLabel
                anchors.verticalCenter: parent.verticalCenter
                visible: buddy.bubbleText.length > 0
                text: buddy.bubbleText
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
            }
        }
    }

    // The microphone is open for the wake word: always shown while it is, never hidden.
    Rectangle {
        id: wakeBadge
        objectName: "wakeBadge"
        visible: Wake.active
        z: 5
        width: Math.round(18 * buddy.s)
        height: width
        radius: width / 2
        x: buddy.width - width + Math.round(2 * buddy.s)
        y: buddy.height - height + Math.round(2 * buddy.s)
        color: Theme.surface
        border.width: 1
        border.color: Wake.state === "awaiting" ? Theme.danger : Theme.borderStrong
        Glyph {
            anchors.centerIn: parent
            name: "mic"
            size: Math.round(11 * buddy.s)
            stroke: 1.8
            color: Wake.state === "awaiting" ? Theme.danger : Theme.textDim
        }
    }

    property string bubbleText: ""
    property string emoteIcon: ""
    Timer {
        id: bubbleTimer
        interval: 1800
        onTriggered: {
            buddy.bubbleText = ""
            buddy.emoteIcon = ""
        }
    }

    function emote(text, icon) {
        bubbleText = text || ""
        emoteIcon = icon || ""
        bubbleTimer.restart()
    }

    // ---- request flow -------------------------------------------------------
    // Where the reply to the request in flight goes: the thought bubble, or
    // the popped-out result panel when the request was asked from there.
    property bool replyToPanel: false

    // The window lost focus (the user clicked somewhere else) or another
    // surface took over. Everything is tucked away, but nothing in flight is
    // cancelled: the orb keeps glowing and the reply pops back up on arrival.
    function stash() {
        const hadSomething = (thought.visible && !thought.closing)
                             || (!!resultPanel && resultPanel.open)
        rememberAnswer()
        thought.close()
        // A request asked from the panel keeps replyToPanel set, so present()
        // puts the answer back in the panel and only then reveals it.
        if (resultPanel)
            resultPanel.open = false
        // Clicked away rather than closed on purpose: let the answer reappear.
        autoPopPending = true
        session = canContinue ? (busy ? "thinking" : "result") : "idle"
        if (!busy)
            setMood("neutral", 0)
        // Only acknowledge an actual fade-out (startup focus loss is silent).
        if (hadSomething && BuddySound)
            BuddySound.play("dismiss")
    }

    // Abandons the request in flight. Only a deliberate user decision (New
    // request, closing the panel that asked, a superseding prompt) may do
    // this — losing window focus must never cut a reply off.
    function cancelPending() {
        thinkTimer.stop()
        if (awaitingReply) {
            awaitingReply = false
            if (BuddyAgent)
                BuddyAgent.cancel()
        }
    }

    // A click on the orb while the bubble is up: collapse it, keep working.
    function stashBubble() {
        if (!thought.visible || thought.closing)
            return
        rememberAnswer()
        thought.close()
        if (session !== "thinking")
            session = canContinue ? (busy ? "thinking" : "result") : "idle"
        if (BuddySound)
            BuddySound.play("dismiss")
    }

    // Keeps what was on screen so "continue" can reopen the exact same answer.
    function rememberAnswer() {
        if (thought.phase !== "answer" || thought.kind === "note")
            return
        lastAnswerKind = thought.kind
        lastAnswerTitle = thought.title
        lastAnswerPayload = thought.payload
        lastAnswerSimulated = thought.simulated
        lastAnswerReveal = thought.progress
    }

    function forgetAnswer() {
        lastAnswerKind = ""
        lastAnswerTitle = ""
        lastAnswerPayload = null
        lastAnswerSimulated = false
        lastAnswerReveal = 1.0
    }

    // The orb was clicked. While a reply is still being produced this only
    // brings the bubble back to watch it; after an interruption it resumes the
    // half-read answer where it stopped; otherwise it opens the prompt.
    // `forcePrompt` is for the explicit "New request" action.
    function openInput(forcePrompt) {
        activationRequested()
        if (!forcePrompt && canContinue) {
            resumeReply()
            return
        }
        openPrompt()
    }

    // Brings back whatever the buddy was in the middle of: the thinking
    // bubble while the model works, or the answer it had not finished
    // revealing.
    function resumeReply() {
        activationRequested()
        autoPopPending = true
        // A reply that belongs to the popped-out panel goes back there first.
        if (resultPanel && replyToPanel
                && (panelHasMoreToRead || resultPanel.phase === "loading" || awaitingReply)) {
            resultPanel.open = true
            resultPanel.clampIntoView()
            session = "result"
            setMood(resultPanel.phase === "loading" ? "thinking" : "speaking", 0)
            hop(55)
            if (BuddySound)
                BuddySound.play("pop")
            return
        }
        // Still waiting on the provider / a local task: show it pondering.
        if (awaitingReply || thinkTimer.running) {
            session = "thinking"
            setMood("thinking", 0)
            hop(55)
            thought.think()
            lookAtBubble(18, 22)
            if (BuddySound)
                BuddySound.play("pop")
            return
        }
        // An answer was on screen when it got tucked away: show it again from
        // the point the reader had reached.
        if (lastAnswerPayload !== null) {
            resumeAnswerBubble()
            return
        }
        openPrompt()
    }

    function resumeAnswerBubble() {
        thought.resumeFrom = lastAnswerReveal
        thought.showAnswer(lastAnswerKind, lastAnswerTitle,
                           lastAnswerPayload, lastAnswerSimulated)
        session = "result"
        setMood(lastAnswerKind === "chat" ? "speaking" : "happy", 0)
        hop(60)
        lookAtBubble(24, 0)
        if (BuddySound)
            BuddySound.play("pop")
    }

    // Plain "type a new question" path. The conversation thread is kept across
    // a collapse, so a follow-up typed after the bubble was hidden still
    // carries the conversation it belongs to.
    function openPrompt() {
        const wasShown = thought.visible && !thought.closing
        cancelPending()
        forgetAnswer()
        if (resultPanel)
            resultPanel.open = false
        session = "input"
        setMood("listening", 0)
        hop(wasShown ? 50 : 85)
        thought.openInput()
        lookAtBubble(26, 4)
        if (BuddySound)
            BuddySound.play(wasShown ? "pop" : "wake")
    }

    // The bubble was closed by its own x / Esc. Even then nothing in flight is
    // thrown away: the orb keeps glowing and the answer pops back when ready.
    // Only a deliberate new request or retry supersedes the work.
    function bubbleClosed() {
        const quiet = thought.kind === "note" && thought.phase === "answer"
        rememberAnswer()
        // The user closed it on purpose: don't pop it back open when the reply
        // lands. It stays resumable from the orb, though.
        autoPopPending = false
        session = resultPanel && resultPanel.open ? "result"
                : (canContinue ? (busy ? "thinking" : "result") : "idle")
        if (!busy)
            setMood("neutral", 0)
        blinkShut = true
        blinkOpen.restart()
        if (!quiet && BuddySound)
            BuddySound.play("dismiss")
    }
    // A click on the orb while its bubble is open just folds it away.
    function dismissInput() {
        stashBubble()
    }
    // Close everything silently (the notch's closeAll, capability actions).
    function dismiss() {
        cancelPending()
        forgetAnswer()
        thought.close()
        if (resultPanel)
            resultPanel.open = false
        session = "idle"
        setMood("neutral", 0)
    }
    // The popped-out panel was closed with its own x. Its answer stays
    // available, and a request it started keeps running.
    function panelClosed() {
        session = thought.visible ? session : (replyToPanel && busy ? "thinking" : "idle")
        if (BuddySound)
            BuddySound.play("dismiss")
    }

    function watchTyping(frac) {
        if (bubbleCentered) {
            const vSign = bubbleAbove ? 1 : -1
            targetYaw = ((frac - 0.5) * 34) * mo
            targetPitch = (vSign * 30 + (Math.random() - 0.5) * 4) * mo
            targetRoll = -((frac - 0.5) * 6) * mo
            glanceAt = wanderT + 1.6
            springing = true
        } else {
            // The caret's distance from the orb sets how far the eyes turn.
            const far = bubbleFlipped ? 1 - frac : frac
            lookAtBubble(14 + 34 * far, -5 + (Math.random() - 0.5) * 5)
        }
        if (mood !== "listening" && mood !== "surprised")
            setMood("listening", 0)
    }

    // ---- slash commands ---------------------------------------------------------
    // "/name args". Built-ins act on Buddy itself and answer locally (nothing is
    // sent to the model, nothing lands in the chat or the history); a user's
    // own command is expanded into its prompt and sent like any other request.
    // Text that only looks like a path ("/usr/bin") is not a command.
    function showLocal(title, text) {
        awaitingReply = false
        autoPopPending = true
        lastRequest = title
        const payload = { response: String(text) }
        if (resultPanel && resultPanel.open) {
            replyToPanel = true
            setPanel("chat", title, payload, false)
            return
        }
        replyToPanel = false
        forgetAnswer()
        thought.showAnswer("chat", title, payload, false)
        session = "result"
        setMood("happy", 1600)
        popForward()
    }
    function whenText(ms) {
        const d = new Date(Number(ms))
        return isNaN(d.getTime()) ? "" : Qt.formatDateTime(d, "d MMM, hh:mm")
    }
    function slashHelp() {
        const lines = [qsTr("**Commands**")]
        const list = BuddyAgent.slashCommands("")
        for (let i = 0; i < list.length; ++i) {
            const c = list[i]
            lines.push("- `/" + c.name + (c.argumentHint ? " " + c.argumentHint : "") + "` — " + c.description
                       + (c.source === "builtin" ? "" : (c.source === "project" ? qsTr(" (this workspace)") : qsTr(" (yours)"))))
        }
        lines.push("")
        lines.push(qsTr("Your own commands are Markdown files in `.noch/commands` of the workspace, or in Noch's data folder under `commands`."))
        return lines.join("\n")
    }
    function chatList() {
        const all = Sessions.sessions
        if (all.length === 0)
            return qsTr("There are no saved chats yet.")
        const lines = [qsTr("**Chats** — type `/resume 2` to pick one up, or `/branch 2` to continue from it in a new chat.")]
        for (let i = 0; i < all.length && i < 12; ++i) {
            const c = all[i]
            lines.push((i + 1) + ". " + c.title + " — " + (c.turns === 1 ? qsTr("1 turn") : qsTr("%1 turns").arg(c.turns)) + ", " + whenText(c.updated)
                       + (c.current ? qsTr("  ← current") : "") + (c.parentTitle ? qsTr("  (branch of %1)").arg(c.parentTitle) : ""))
        }
        return lines.join("\n")
    }
    // "2", or the start of an id or title. Returns the chat's id, or "".
    function chatByArg(arg) {
        const all = Sessions.sessions
        const a = String(arg || "").trim().toLowerCase()
        if (a.length === 0)
            return ""
        if (/^\d+$/.test(a)) {
            const n = parseInt(a, 10) - 1
            return n >= 0 && n < all.length ? all[n].id : ""
        }
        for (let i = 0; i < all.length; ++i)
            if (String(all[i].id).toLowerCase().indexOf(a) === 0 || String(all[i].title).toLowerCase().indexOf(a) === 0)
                return all[i].id
        return ""
    }
    // Show where a chat stands: its last exchange, so it is clear what was picked up.
    function showResumed(prefix) {
        conversation = Sessions.context()
        const last = Sessions.lastTurn()
        if (!last || !last.q) {
            showLocal(Sessions.currentTitle, prefix)
            return
        }
        showLocal(Sessions.currentTitle, prefix + "\n\n**" + last.q + "**\n\n" + last.a)
        lastRequest = last.q
    }
    function instructionsText() {
        const ins = BuddyAgent.projectInstructions()
        if (ins.empty)
            return qsTr("Buddy is not reading any instruction files here. `/init` creates a NOCH.md in this workspace.")
        const lines = [qsTr("**Buddy is reading**")]
        for (let i = 0; i < ins.sources.length; ++i) {
            const src = ins.sources[i]
            lines.push("- " + src.name + " — " + Math.round(src.bytes / 102.4) / 10 + " KB" + (src.truncated ? qsTr(" (cut short)") : ""))
        }
        if (ins.truncated)
            lines.push(qsTr("Some of it was too long and is cut short."))
        return lines.join("\n")
    }
    function runSlash(cmd, typed) {
        if (cmd.kind === "prompt") {
            submit(cmd.prompt, true)
            return
        }
        if (cmd.kind === "unknown") {
            showLocal(typed, cmd.message)
            return
        }
        const args = String(cmd.args || "").trim()
        switch (cmd.name) {
        case "help":
            showLocal(qsTr("Commands"), slashHelp())
            break
        case "new":
            cancelPending()
            Sessions.newChat()
            conversation = []
            note(qsTr("New chat"), "spark")
            break
        case "resume": {
            if (args.length === 0) {
                showLocal(qsTr("Chats"), chatList())
                break
            }
            const id = chatByArg(args)
            if (id.length === 0 || !Sessions.open(id)) {
                showLocal(typed, qsTr("I couldn't find that chat.") + "\n\n" + chatList())
                break
            }
            showResumed(qsTr("Picked up where you left off."))
            break
        }
        case "branch": {
            const from = args.length > 0 ? chatByArg(args) : ""
            if (args.length > 0 && from.length === 0) {
                showLocal(typed, qsTr("I couldn't find that chat.") + "\n\n" + chatList())
                break
            }
            if (from.length === 0 && Sessions.currentTurns === 0) {
                showLocal(typed, qsTr("There is nothing to branch from yet. Ask something first, or pick a chat: `/branch 2`."))
                break
            }
            if (Sessions.branch(from).length === 0) {
                showLocal(typed, qsTr("I couldn't branch that chat."))
                break
            }
            showResumed(qsTr("Branched into a new chat. The original is unchanged."))
            break
        }
        case "compact":
            if (Sessions.requestSummary())
                note(qsTr("Summarizing this chat…"), "spark")
            else
                note(qsTr("Nothing to summarize yet"), "check")
            break
        case "undo": {
            const force = args.toLowerCase() === "force"
            const r = BuddyAgent.undoLastRun(force)
            let msg = r.message
            if (!force && r.conflicts && r.conflicts.length > 0)
                msg += "\n\n" + qsTr("Files you changed since are left as they are. `/undo force` puts the old versions back anyway.")
            showLocal(typed, msg)
            break
        }
        case "mode": {
            const names = { "ask": "default", "default": "default", "auto-edit": "acceptEdits", "acceptedits": "acceptEdits",
                            "plan": "plan", "trusted": "bypass", "bypass": "bypass" }
            const labels = { "default": qsTr("Ask first"), "acceptEdits": qsTr("Auto-edit"), "plan": qsTr("Plan only"), "bypass": qsTr("Trusted — no prompts") }
            if (args.length === 0) {
                showLocal(typed, qsTr("Buddy is in **%1** mode. Change it with `/mode ask`, `/mode auto-edit`, `/mode plan` or `/mode trusted`.")
                          .arg(labels[BuddyAgent.permissionMode] || BuddyAgent.permissionMode))
                break
            }
            const target = names[args.toLowerCase()]
            if (!target) {
                showLocal(typed, qsTr("Modes are: ask, auto-edit, plan, trusted."))
                break
            }
            permissionModeRequested(target)
            note(qsTr("Mode: %1").arg(labels[target]), "check")
            break
        }
        case "init":
            showLocal(typed, BuddyAgent.initProjectInstructions().message)
            break
        case "instructions":
            showLocal(typed, instructionsText())
            break
        default:
            showLocal(typed, qsTr("I don't know how to run /%1.").arg(cmd.name))
        }
    }

    function submit(text, plain) {
        const trimmed = String(text || "").trim()
        if (trimmed.length === 0 || session === "thinking")
            return
        if (!plain && trimmed.charAt(0) === "/" && BuddyAgent) {
            const cmd = BuddyAgent.resolveSlash(trimmed)
            if (cmd.kind !== "none") {
                runSlash(cmd, trimmed)
                return
            }
        }
        spokenRun = voiceStarted
        lastRequest = trimmed
        forgetAnswer()
        autoPopPending = true
        replyToPanel = !!resultPanel && resultPanel.open
        session = "thinking"
        gulp()
        setMood("thinking", 0)
        if (!replyToPanel)
            thought.think()
        lookAtBubble(18, 22)
        if (BuddySound)
            BuddySound.play("think")

        // Direct shell command shortcut ($ <cmd>, bash: <cmd>, powershell: <cmd>, cmd: <cmd>)
        const directMatch = trimmed.match(/^(?:\$|!|bash:|powershell:|ps:|cmd:)\s*(.+)$/i)
        if (directMatch && BuddyAgent && BuddyAgent.allowBash) {
            if (replyToPanel) {
                resultPanel.kind = "chat"
                resultPanel.title = trimmed
                resultPanel.userPrompt = trimmed
                resultPanel.payload = { response: "" }
                resultPanel.simulated = false
                resultPanel.phase = "loading"
            }
            awaitingReply = true
            BuddyAgent.runDirectCommand(directMatch[1])
            return
        }

        // Direct web search shortcut (search: <query>, web: <query>)
        const webMatch = trimmed.match(/^(?:search:|web:)\s*(.+)$/i)
        if (webMatch && BuddyAgent && BuddyAgent.allowWebSearch) {
            if (replyToPanel) {
                resultPanel.kind = "chat"
                resultPanel.title = trimmed
                resultPanel.userPrompt = trimmed
                resultPanel.payload = { response: "" }
                resultPanel.simulated = false
                resultPanel.phase = "loading"
            }
            awaitingReply = true
            BuddyAgent.runDirectWebSearch(webMatch[1])
            return
        }

        // Direct tool search shortcut (tools: <query>, tool_search: <query>, regex_tools: <pattern>)
        const toolSearchMatch = trimmed.match(/^(?:tools:|tool_search:|tool-search:|regex_tools:)\s*(.+)$/i)
        if (toolSearchMatch && BuddyAgent && BuddyAgent.allowToolSearch) {
            if (replyToPanel) {
                resultPanel.kind = "chat"
                resultPanel.title = trimmed
                resultPanel.userPrompt = trimmed
                resultPanel.payload = { response: "" }
                resultPanel.simulated = false
                resultPanel.phase = "loading"
            }
            awaitingReply = true
            const useRegex = /^regex_tools:/i.test(trimmed)
            BuddyAgent.runDirectToolSearch(toolSearchMatch[1], useRegex ? "regex" : "bm25")
            return
        }

        // Direct Advisor consultation shortcut (advisor: <question>, advise: <question>)
        const advisorMatch = trimmed.match(/^(?:advisor:|advise:|consult:)\s*(.+)$/i)
        if (advisorMatch && BuddyAgent && BuddyAgent.allowAdvisor) {
            if (replyToPanel) {
                resultPanel.kind = "chat"
                resultPanel.title = trimmed
                resultPanel.userPrompt = trimmed
                resultPanel.payload = { response: "" }
                resultPanel.simulated = false
                resultPanel.phase = "loading"
            }
            awaitingReply = true
            BuddyAgent.runDirectAdvisor(advisorMatch[1], "general")
            return
        }

        // Direct desktop actions (timers, simple app launch, day plan) run locally,
        // unless Agent SDK mode is enabled and the prompt asks for multi-step/tool work.
        const localTask = router.resolve(trimmed)
        const compoundAgentIntent = /\b(and|then|command|powershell|bash|terminal|shell|click|screenshot|screen|cursor|mouse|type|press|key|file|folder|directory|read|write|create|edit|script|memory|memories|remember|search|web|url|http|https|grep|glob|advisor|advise|consult|tool|clipboard|cpu|ram|system|todo|checklist)\b/i.test(trimmed)
        const local = !!localTask
                      && (localTask.kind === "timer" || localTask.kind === "apps" || localTask.kind === "tasks")
                      && !(BuddyAgent && BuddyAgent.enabled && BuddyAgent.agentMode
                           && (localTask.kind === "apps" || localTask.kind === "tasks")
                           && compoundAgentIntent)
        if (!local && BuddyAgent && BuddyAgent.enabled) {
            if (replyToPanel) {
                resultPanel.kind = "chat"
                resultPanel.title = trimmed
                resultPanel.userPrompt = trimmed
                resultPanel.payload = { response: "" }
                resultPanel.simulated = false
                resultPanel.phase = "loading"
            }
            awaitingReply = true
            BuddyAgent.sendMessage(trimmed, conversation)
            return
        }
        if (BuddyAgent)
            BuddyAgent.clearToolSteps()
        thinkTimer.restart()
    }
    function retryTask() {
        if (lastRequest.length > 0) {
            cancelPending()
            session = "idle"
            submit(lastRequest)
        }
    }
    function showError(message) {
        awaitingReply = false
        if (spokenRun) {
            Speech.say(qsTr("That didn't work."))
            spokenRun = false
        }
        if (replyToPanel && resultPanel) {
            resultPanel.errorMessage = message
            resultPanel.phase = "error"
            resultPanel.open = true
        } else if (autoPopPending || !thought.visible) {
            thought.showError(message)
        }
        session = "result"
        // A sympathetic droop: squinted eyes, head tilted, looking down.
        if (react("error", 2600, 5, 0))
            shake()
        targetRoll = -10 * mo
        targetPitch = -12 * mo
        springing = true
        if (BuddySound)
            BuddySound.play("error")
    }
    // Local tasks still think for a beat so the bubble's morph can be seen.
    Timer { id: thinkTimer; interval: 820; onTriggered: buddy.runTask() }
    function parsePlan(json) {
        try { return JSON.parse(json) } catch (e) { return null }
    }
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
            present("chart", task.title, chart, true)
            break
        }
        case "research":
            present("research", task.title, task.payload, true)
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
            present("tasks", task.title, { items: list, day: day }, true)
            break
        }
        case "timer": {
            if (focusTimer) {
                focusTimer.setMinutes(task.payload.minutes)
                focusTimer.start()
            }
            note(qsTr("Focus timer set · %1 min").arg(task.payload.minutes), "timer")
            break
        }
        case "apps": {
            const rows = AppIndex.search(task.payload.query)
                            .filter(function (r) { return r.kind === "app" })
            if (rows.length > 0) {
                AppIndex.activate(rows[0])
                note(qsTr("Opening %1").arg(String(rows[0].name || task.payload.query)), "open")
            } else {
                showError(qsTr("No app matched “%1”.").arg(task.payload.query))
            }
            break
        }
        default:
            present("help", task.title, {}, false)
            break
        }
    }

    // Show a finished result: in the bubble, or in the panel it came from.
    function present(kind, title, payload, simulated) {
        awaitingReply = false
        autoPopPending = true
        if (kind === "chat" && payload && payload.response)
            reportAloud(payload.response)
        else
            spokenRun = false
        recordHistory(kind, title, payload)
        if (kind === "chat" && payload && payload.response)
            rememberTurn(lastRequest, payload.response)
        if (replyToPanel && resultPanel) {
            setPanel(kind, title, payload, simulated)
            return
        }
        // The bubble may have been tucked away while the reply was still being
        // made. Pop it back out unless the user closed it on purpose; either
        // way the answer is kept so the orb can resume it.
        const reopen = autoPopPending || !thought.visible
        if (reopen) {
            thought.showAnswer(kind, title, payload, simulated)
            session = "result"
            popForward()
            return
        }
        rememberAnswerFrom(kind, title, payload, simulated)
        session = canContinue ? "result" : "idle"
        setMood("happy", 1600)
        neonBurst(true)
    }
    // Caches an answer that arrived while the bubble was deliberately closed.
    function rememberAnswerFrom(kind, title, payload, simulated) {
        if (kind === "note")
            return
        lastAnswerKind = kind
        lastAnswerTitle = title
        lastAnswerPayload = payload
        lastAnswerSimulated = simulated
        lastAnswerReveal = 0
    }
    // Result arriving from a tucked-away state: a fuller entrance (it may be
    // off-screen focus, so make the arrival obvious).
    function popForward() {
        eureka()
        if (BuddySound)
            BuddySound.play("reply")
    }
    // A quick confirmation that tucks itself away.
    function note(text, icon) {
        awaitingReply = false
        forgetAnswer()
        if (autoPopPending || !thought.visible)
            thought.showNote(text, icon)
        session = "result"
        setMood("happy", 2000)
        hop(105)
        neonBurst(false)
        lookAtBubble(22, 0)
        if (BuddySound)
            BuddySound.play("reply")
    }
    function setPanel(kind, title, payload, simulated) {
        if (!resultPanel)
            return
        forgetAnswer()
        resultPanel.kind = kind
        resultPanel.title = title
        resultPanel.userPrompt = lastRequest
        resultPanel.payload = payload
        resultPanel.simulated = simulated
        resultPanel.phase = "ready"
        resultPanel.resetBody()
        resultPanel.clampIntoView()
        resultPanel.open = true
        session = "result"
        setMood(kind === "chat" ? "speaking" : "happy", 2400)
        hop(115)
        if (BuddySound)
            BuddySound.play("reply")
    }
    // Pop the bubble's answer out into the movable, resizable panel, right
    // where the bubble was.
    function expandToPanel() {
        if (!resultPanel || thought.phase !== "answer" || thought.kind === "note")
            return
        const k = thought.kind
        const ttl = thought.title
        const data = thought.payload
        const sim = thought.simulated
        const at = thought.mapToItem(desktop, thought.bx, thought.by)
        thought.close()
        resultPanel.kind = k
        resultPanel.title = ttl
        resultPanel.userPrompt = lastRequest
        resultPanel.payload = data
        resultPanel.simulated = sim
        resultPanel.phase = "ready"
        // It was already read in the bubble; no second typewriter pass.
        thought.revealAll()
        forgetAnswer()
        resultPanel.revealedChars = resultPanel.fullResponse.length
        resultPanel.isStreaming = false
        resultPanel.dragX = Math.round(at.x)
        resultPanel.dragY = Math.round(at.y)
        resultPanel.resetBody()
        resultPanel.clampIntoView()
        resultPanel.open = true
        session = "result"
        setMood("happy", 1400)
        if (BuddySound)
            BuddySound.play("pop")
    }

    // ---- capability actions -------------------------------------------------
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
            toastRequested("25-minute focus started")
            break
        case "clipboard":
            dismiss()
            clipboardRequested()
            break
        case "mic":
            Audio.toggleMicMute()
            dismiss()
            toastRequested(Audio.micMuted ? "Microphone muted" : "Microphone live")
            break
        case "settings":
            dismiss()
            settingsRequested()
            break
        }
    }

    // ---- thought bubble -----------------------------------------------------
    // Opens on the side with room for the widest answer card, so it never
    // has to jump sides halfway through a request. When the buddy is in or
    // around the horizontal center (top, middle, or bottom), the popup sits
    // on the buddy's head; normally it sits at ~45° above or below the buddy.
    readonly property real bubbleRoom: 440 + Math.round(34 * s) + 16
    readonly property real buddyCenterX: x + width / 2
    readonly property bool bubbleCentered: !!desktop
        && Math.abs(buddyCenterX - desktop.width * 0.5) <= desktop.width * 0.18
    readonly property bool bubbleFlipped: !bubbleCentered && !!desktop
        && (desktop.width - (x + width)) < bubbleRoom
        && x > (desktop.width - (x + width))
    readonly property bool bubbleAbove: !!desktop
        ? (baseY >= Math.max(Math.round(80 * s), desktop.height * 0.22)
           && (!bubbleCentered || baseY >= thought.targetH + Math.round(22 * s) + 12
               || baseY >= desktop.height - (baseY + height)))
        : true

    BuddyBubble {
        id: thought
        objectName: "buddyBubble"
        s: buddy.s
        orbSize: buddy.orbSize
        flipped: buddy.bubbleFlipped
        centered: buddy.bubbleCentered
        above: buddy.bubbleAbove
        mo: buddy.mo
        bounds: buddy.desktop
                ? Qt.rect(8 - buddy.x, 8 - buddy.y, buddy.desktop.width - 16, buddy.desktop.height - 16)
                : Qt.rect(-100000, -100000, 200000, 200000)

        onSubmitted: (text) => buddy.submit(text)
        onDismissed: buddy.bubbleClosed()
        onRetryRequested: buddy.retryTask()
        onTyped: (frac) => buddy.watchTyping(frac)
        onLoved: buddy.celebrate()
        onSpeakingTick: (p) => buddy.readAlong(p)
        onRevealFinished: buddy.answerSettled()
        onExpandRequested: buddy.expandToPanel()
        onPlanEdited: (items) => buddy.planEdited(items)
        onCapability: (id) => buddy.runCapability(id)
        history: buddy.history
        onHistoryPicked: (entry) => buddy.replayHistory(entry)
        onHistoryCleared: buddy.clearHistory()
        onHistoryOpened: {
            buddy.setMood("curious", 1400)
            buddy.lookAtBubble(26, -10)
        }
    }

    // ---- click / drag / pet handling ----------------------------------------
    MouseArea {
        id: buddyHit
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: pressed && dragged ? Qt.ClosedHandCursor : Qt.PointingHandCursor

        property real startDesktopX: 0
        property real startDesktopY: 0
        property real startOriginY: 0
        property real prevDragX: 0
        property real prevDragY: 0
        property bool dragged: false

        onEntered: {
            buddy.lastHoverX = mouseX
            buddy.lastHoverY = mouseY
            buddy.springing = true
            buddy.wake()
            dozeTimer.restart()
        }
        onPressed: (mouse) => {
            buddy.wake()
            dozeTimer.restart()
            const g = buddy.desktop
                      ? buddyHit.mapToItem(buddy.desktop, mouse.x, mouse.y)
                      : Qt.point(buddy.x + mouse.x, buddy.y + mouse.y)
            startDesktopX = g.x
            startDesktopY = g.y
            startOriginY = buddy.baseY
            prevDragX = g.x
            prevDragY = g.y
            dragged = false
            buddy.dragOffsetX = 0
            buddy.dragOffsetY = 0
            buddy.squashX = 1.08
            buddy.squashY = 0.90
            buddy.springing = true
        }
        onPositionChanged: (mouse) => {
            if (!pressed) {
                // Detect gentle petting / rubbing across the orb when hovering
                const step = Math.hypot(mouse.x - buddy.lastHoverX, mouse.y - buddy.lastHoverY)
                buddy.lastHoverX = mouse.x
                buddy.lastHoverY = mouse.y
                if (step > 2 && step < 40) {
                    buddy.petAccum += step
                    if (buddy.petAccum > 210) {
                        buddy.petAccum = 0
                        buddy.celebrate()
                    }
                }
                return
            }
            const g = buddy.desktop
                      ? buddyHit.mapToItem(buddy.desktop, mouse.x, mouse.y)
                      : Qt.point(buddy.x + mouse.x, buddy.y + mouse.y)
            const dx = g.x - startDesktopX
            const dy = g.y - startDesktopY
            if (!dragged && Math.hypot(dx, dy) > 5) {
                dragged = true
                buddy.setMood("surprised", 0)
            }
            if (dragged) {
                buddy.dragVelX = g.x - prevDragX
                buddy.dragVelY = g.y - prevDragY
                prevDragX = g.x
                prevDragY = g.y

                const maxW = buddy.desktop ? buddy.desktop.width : 1920
                const maxH = buddy.desktop ? buddy.desktop.height : 1080
                const targetX = Math.max(8, Math.min(maxW - buddy.width - 8, buddy.baseX + dx))
                const targetY = Math.max(8, Math.min(maxH - buddy.height - 8, startOriginY + dy))
                buddy.dragOffsetX = targetX - buddy.baseX
                buddy.dragOffsetY = targetY - buddy.baseY

                // Eyes look into the direction of motion while being carried
                buddy.targetYaw = Math.max(-buddy.yawMax, Math.min(buddy.yawMax, buddy.dragVelX * 3.2))
                buddy.targetPitch = Math.max(-buddy.pitchMax, Math.min(buddy.pitchMax, -buddy.dragVelY * 3.2))
                buddy.springing = true
            }
        }
        onReleased: () => {
            if (dragged) {
                const newAnchorX = Math.round(buddy.x + buddy.width / 2)
                const newAnchorY = Math.round(buddy.y)
                buddy.dragOffsetX = 0
                buddy.dragOffsetY = 0
                dragged = false
                buddy.squashX = 1.15
                buddy.squashY = 0.86
                buddy.hop(75)
                buddy.setMood("happy", 1400)
                buddy.neonBurst(false)
                if (BuddySound)
                    BuddySound.play("drop")
                buddy.buddyMoved(newAnchorX, newAnchorY)
            } else {
                // The orb toggles its thought bubble.
                if (thought.visible && !thought.closing)
                    buddy.dismissInput()
                else
                    buddy.openInput()
            }
        }
        onCanceled: () => {
            buddy.dragOffsetX = 0
            buddy.dragOffsetY = 0
            dragged = false
            buddy.setMood("neutral", 0)
        }
    }

    // ---- real chat provider -------------------------------------------------
    // Push-to-talk. What was said goes into the field, ready to check and send;
    // with "send right away" on, it is sent as it is. The words are only ever
    // text from here on: the audio stays inside VoiceInput.
    function voiceHeard(text) {
        const said = String(text || "").trim()
        if (said.length === 0)
            return
        // While a request is running the words have nowhere to go; a note would
        // cut that request's reply off, so they are dropped.
        if (busy || session === "thinking") {
            if (BuddySound)
                BuddySound.play("dismiss")
            return
        }
        if (voiceAutoSend) {
            submit(said)
            return
        }
        if (!(thought.visible && thought.phase === "input"))
            openPrompt()
        Qt.callLater(function () { thought.insertText(said) })
    }
    Connections {
        target: Voice
        function onTranscript(text) { buddy.voiceHeard(text) }
        function onFailed(message) {
            if (message.length > 0 && !buddy.busy)
                buddy.note(message, "mic-off")
        }
    }

    // ---- wake word ------------------------------------------------------------
    // "Hey buddy, <request>": the request runs like a typed one (same permission
    // gate, same approvals), and because it was asked out loud its outcome is
    // also said out loud when that is switched on in Settings.
    property bool voiceStarted: false     // the next submit() comes from the wake word
    property bool spokenRun: false        // the run in flight does
    function wakeHeard() {
        wake()
        react("curious", 1400, 4, 70)
        lookAtBubble(24, 4)
        if (BuddySound)
            BuddySound.play("wake")
        // A note would cut a running request's reply off; only show one when idle.
        if (!busy && session !== "thinking")
            note(qsTr("Listening…"), "mic")
    }
    function wakeCommand(text) {
        const said = String(text || "").trim()
        if (said.length === 0)
            return
        if (busy || session === "thinking") {
            if (BuddySound)
                BuddySound.play("dismiss")
            Speech.say(qsTr("I'm still busy with the last request."))
            return
        }
        voiceStarted = true
        submit(said)
        voiceStarted = false
    }
    function reportAloud(reply) {
        if (spokenRun)
            Speech.speakReport(reply)
        spokenRun = false
    }
    Connections {
        target: Wake
        function onWoke() { buddy.wakeHeard() }
        function onCommand(text) { buddy.wakeCommand(text) }
        function onTimedOut() {
            if (!buddy.busy)
                buddy.note(qsTr("I didn't catch a request"), "mic-off")
        }
        function onFailed(message) {
            if (message.length > 0 && !buddy.busy)
                buddy.note(message, "mic-off")
        }
    }

    // Only the request the buddy is still waiting for may answer; a reply to
    // a question that was closed or superseded is dropped.
    Connections {
        target: BuddyAgent
        function onResponseReady(text) {
            if (!buddy.awaitingReply)
                return
            const steps = BuddyAgent.toolSteps || []
            let clean = steps.length > 0
            for (let i = 0; i < steps.length; ++i)
                if (steps[i].status === "error") { clean = false; break }
            if (clean)
                buddy.nodHappy()
            const streamed = String(BuddyAgent.streamText || "").length > 0
            if (streamed && buddy.replyToPanel && buddy.resultPanel)
                buddy.resultPanel.instantReveal = true
            buddy.present("chat", buddy.lastRequest.length > 0 ? buddy.lastRequest : "Buddy",
                          { response: text }, false)
            if (streamed && thought.phase === "answer" && thought.kind === "chat")
                thought.revealAll()
        }
        function onErrorOccurred(message) {
            if (!buddy.awaitingReply)
                return
            buddy.showError(message)
        }
        // The agent paused on a tool call that needs the user's OK: perk up,
        // look at the bubble, and nudge again every few seconds until answered.
        function onPendingApprovalChanged() {
            const p = BuddyAgent.pendingApproval
            const waiting = p && p.id !== undefined
            if (waiting && !approvalNudge.running) {
                if (buddy.spokenRun)
                    Speech.say(qsTr("I need your approval to continue."))
                buddy.wake()
                buddy.react("surprised", 900, 4, 80)
                buddy.lookAtBubble(24, 4)
                if (BuddySound)
                    BuddySound.play("pop")
            }
            buddy.refreshNudge()
        }
        // Buddy has a question for the user: the same perk-up and nudging as
        // for an approval, once per question (the countdown republishes it
        // every second).
        function onPendingQuestionChanged() {
            const p = BuddyAgent.pendingQuestion
            const asking = !!(p && p.id !== undefined)
            if (asking && !buddy.questionShown) {
                buddy.questionShown = true
                if (buddy.spokenRun)
                    Speech.say(qsTr("I have a question for you."))
                buddy.wake()
                buddy.react("curious", 1100, 4, 70)
                buddy.lookAtBubble(24, 4)
                if (BuddySound)
                    BuddySound.play("pop")
            } else if (!asking) {
                buddy.questionShown = false
            }
            buddy.refreshNudge()
        }
        function onToolStepsChanged() {
            const steps = BuddyAgent.toolSteps || []
            if (!buddy.awaitingReply || steps.length === 0)
                return
            const last = steps[steps.length - 1]
            if (last.status === "error" && buddy.lastErrorStep !== last.id) {
                buddy.lastErrorStep = String(last.id)
                if (buddy.react("error", 900, 5, 0))   // a brief wince; the run carries on
                    buddy.shake()
            }
        }
        function onToolExecuted(name, summary) {
            if (!buddy.awaitingReply)
                return
            if (buddy.react("curious", 1600, 2, 42))
                buddy.lookAtBubble(22, 6)
            if (BuddySound)
                BuddySound.play("pop")
        }
    }

    property string lastErrorStep: ""
    property bool questionShown: false
    // Nudge every few seconds while something waits on the user.
    function refreshNudge() {
        const a = BuddyAgent ? BuddyAgent.pendingApproval : null
        const q = BuddyAgent ? BuddyAgent.pendingQuestion : null
        const waiting = !!((a && a.id !== undefined) || (q && q.id !== undefined))
        approvalNudge.running = waiting && buddy.visible && !buddy.motionPaused
    }
    Timer {
        id: approvalNudge
        interval: 6000
        repeat: true
        onTriggered: buddy.react("curious", 1200, 4, 48)
    }

    Component.onCompleted: computeEyePoses()
}
