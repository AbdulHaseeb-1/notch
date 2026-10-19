pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Buddy listening. Rings of sound draw in towards the orb, and while the
// microphone is live a ribbon of three coloured waves round it follows the
// voice, so what is heard is plainly seen.
//
// `mic` false is the quiet version (Buddy is waiting for something typed): the
// rings only, slow and faint.
Item {
    id: fx

    property real orbRadius: 26
    property real s: 1
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    property bool active: false
    property bool mic: false          // the microphone is open: show the voice ribbon
    property real level: 0            // 0 .. 1 from the microphone
    property bool showRings: true
    property bool showWave: true
    property real calm: 1             // 1, or less for reduced motion

    readonly property bool live: active && visible
    visible: opacity > 0.01
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: active ? 260 : 380; easing.type: Easing.OutCubic } }

    readonly property real baseR: orbRadius + 7 * s
    readonly property real ampMax: 6.5 * s

    // ---- sound drawing in -----------------------------------------------------------
    Repeater {
        model: fx.showRings ? 3 : 0
        BuddyNeonRing {
            id: ringIn
            required property int index
            readonly property real outer: fx.orbRadius + 20 * fx.s
            width: (outer + 2 * fx.s) * 2
            height: width
            anchors.centerIn: parent
            midR: outer
            thickness: Math.max(1.1, 1.5 * fx.s)
            stops: fx.stops
            opacity: 0
            transformOrigin: Item.Center
            SequentialAnimation {
                running: fx.live
                PauseAnimation { duration: ringIn.index * (fx.mic ? 520 : 900) }
                ParallelAnimation {
                    loops: Animation.Infinite
                    ScaleAnimator {
                        target: ringIn
                        from: 1
                        to: (fx.orbRadius + 1.5 * fx.s) / ringIn.outer
                        duration: fx.mic ? 1560 : 2700
                        easing.type: Easing.InQuad
                    }
                    SequentialAnimation {
                        OpacityAnimator { target: ringIn; from: 0; to: fx.mic ? 0.8 : 0.4; duration: fx.mic ? 380 : 700; easing.type: Easing.OutQuad }
                        OpacityAnimator { target: ringIn; to: 0; duration: fx.mic ? 1180 : 2000; easing.type: Easing.InQuad }
                    }
                }
            }
        }
    }

    // ---- the voice ribbon -----------------------------------------------------------------
    property real phase: 0
    property real lv: 0               // the level, smoothed
    property var ptsA: []
    property var ptsB: []
    property var ptsC: []
    readonly property int steps: 72

    function ribbon(k, a, ph) {
        const out = new Array(steps + 1)
        const n = steps
        for (let i = 0; i <= n; ++i) {
            const t = i / n * Math.PI * 2
            let w
            if (k === 0)
                w = Math.sin(t * 3 + ph * 1.15) * 0.55 + Math.sin(t * 5 - ph * 0.8) * 0.3 + Math.sin(t * 2 + ph * 0.5) * 0.15
            else if (k === 1)
                w = Math.sin(t * 4 - ph * 1.3 + 1.3) * 0.5 + Math.sin(t * 2 + ph * 0.9) * 0.35 + Math.sin(t * 7 + ph * 0.6) * 0.15
            else
                w = Math.sin(t * 5 + ph * 0.95 + 2.4) * 0.45 + Math.sin(t * 3 - ph * 1.1) * 0.35 + Math.sin(t * 8 - ph * 0.5) * 0.2
            const r = baseR + w * a
            out[i] = Qt.point(Math.cos(t) * r, Math.sin(t) * r)
        }
        return out
    }

    FrameAnimation {
        running: fx.live && fx.mic && fx.showWave
        onTriggered: {
            const dt = Math.min(0.05, frameTime)
            // The level jumps about; the ribbon should flow. A little attack, a lazier release.
            const target = Math.min(1, fx.level * 1.25)
            fx.lv += (target - fx.lv) * Math.min(1, dt * (target > fx.lv ? 14 : 5))
            fx.phase += dt * (1.5 + 3.2 * fx.lv) * fx.calm
            const a = fx.ampMax * (0.16 + 0.84 * fx.lv) * fx.calm
            fx.ptsA = fx.ribbon(0, a, fx.phase)
            fx.ptsB = fx.ribbon(1, a * 0.82, fx.phase * 1.07)
            fx.ptsC = fx.ribbon(2, a * 0.64, fx.phase * 0.93)
        }
    }

    component Wave: Shape {
        id: wave
        property var points: []
        property color color: "white"
        property real w: 1.6
        property real a: 0.9
        anchors.centerIn: parent
        width: 1
        height: 1
        opacity: a
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: wave.color
            strokeWidth: wave.w
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: wave.points }
        }
    }

    Item {
        anchors.centerIn: parent
        width: 1
        height: 1
        opacity: fx.mic && fx.showWave ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 240 } }

        // soft under-glow
        Wave { points: fx.ptsA; color: Qt.color(fx.stops[0]); w: 7 * fx.s; a: 0.10 }
        Wave { points: fx.ptsB; color: Qt.color(fx.stops[2]); w: 7 * fx.s; a: 0.08 }
        Wave { points: fx.ptsC; color: Qt.color(fx.stops[1]); w: 7 * fx.s; a: 0.08 }
        // the ribbon
        Wave { points: fx.ptsC; color: Qt.color(fx.stops[1]); w: Math.max(1.1, 1.25 * fx.s); a: 0.8 }
        Wave { points: fx.ptsB; color: Qt.color(fx.stops[2]); w: Math.max(1.1, 1.25 * fx.s); a: 0.85 }
        Wave { points: fx.ptsA; color: Qt.color(fx.stops[0]); w: Math.max(1.3, 1.6 * fx.s); a: 1.0 }
    }
}
