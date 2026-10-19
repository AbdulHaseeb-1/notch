pragma ComponentBehavior: Bound
import QtQuick

// Buddy speaking. A crown of bars round the orb dances to the voice, and rings
// of sound go out from it on every beat.
//
// There is no audio level to read from a voice that is synthesised elsewhere (or
// from words being typed out), so the energy is a speech-shaped envelope: bursts
// the length of a syllable with a breath between phrases. `energy` follows the
// real thing when it is known.
Item {
    id: fx

    property real orbRadius: 26
    property real s: 1
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    property bool active: false
    property real energy: -1          // 0 .. 1 when known; -1: make one up
    property bool showBars: true
    property bool showRings: true
    property real calm: 1

    readonly property bool live: active && visible
    visible: opacity > 0.01
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: active ? 200 : 420; easing.type: Easing.OutCubic } }

    readonly property int count: 44
    readonly property real innerR: orbRadius + 5 * s
    readonly property real maxLen: 13 * s

    // ---- speech-shaped energy ---------------------------------------------------------
    property real env: 0
    property real target: 0
    property real phase: 0
    property int syllables: 0
    readonly property bool synthetic: energy < 0

    Timer {
        // A syllable is a beat of ~130-220 ms; every so often a pause, as between phrases.
        interval: 150
        repeat: true
        running: fx.live && fx.synthetic
        onTriggered: {
            fx.syllables++
            const breath = fx.syllables % (9 + Math.floor(Math.random() * 5)) === 0
            fx.target = breath ? 0.06 : 0.35 + Math.random() * 0.65
            interval = breath ? 330 : 110 + Math.random() * 120
        }
    }

    FrameAnimation {
        running: fx.live && fx.showBars
        onTriggered: {
            const dt = Math.min(0.05, frameTime)
            const goal = fx.synthetic ? fx.target : fx.energy
            fx.env += (goal - fx.env) * Math.min(1, dt * (goal > fx.env ? 16 : 7))
            fx.phase += dt * (2.2 + 4 * fx.env) * fx.calm
        }
    }

    // ---- the crown of bars --------------------------------------------------------------------
    Repeater {
        model: fx.showBars ? fx.count : 0
        Item {
            id: bar
            required property int index
            readonly property real a: index / fx.count * Math.PI * 2
            // Each bar rides its own mix of waves, so the crown ripples instead of pumping.
            readonly property real wob: 0.5 + 0.5 * (Math.sin(a * 3 + fx.phase * 1.3) * 0.6 + Math.sin(a * 7 - fx.phase * 2.1) * 0.4)
            readonly property real len: fx.s * 1.6 + fx.maxLen * fx.env * (0.18 + 0.82 * wob) * fx.calm
            anchors.centerIn: parent
            width: 0
            height: 0
            rotation: index * 360 / fx.count
            Rectangle {
                readonly property real w: Math.max(1.6, 2.1 * fx.s)
                x: -w / 2
                y: -(fx.innerR + bar.len)
                width: w
                height: bar.len
                radius: w / 2
                color: {
                    const t = bar.index / fx.count * fx.stops.length
                    const i = Math.floor(t) % fx.stops.length
                    const f = t - Math.floor(t)
                    const c = Qt.color(fx.stops[i])
                    const d = Qt.color(fx.stops[(i + 1) % fx.stops.length])
                    return Qt.rgba(c.r + (d.r - c.r) * f, c.g + (d.g - c.g) * f, c.b + (d.b - c.b) * f, 1)
                }
                opacity: 0.55 + 0.45 * Math.min(1, bar.len / (fx.maxLen * 0.8))
            }
        }
    }

    // ---- sound going out --------------------------------------------------------------------
    Repeater {
        model: fx.showRings ? 3 : 0
        BuddyNeonRing {
            id: out
            required property int index
            readonly property real inner: fx.orbRadius + 4 * fx.s
            width: (inner + 3 * fx.s) * 2
            height: width
            anchors.centerIn: parent
            midR: inner
            thickness: Math.max(1.4, 2.2 * fx.s)
            stops: fx.stops
            opacity: 0
            transformOrigin: Item.Center
            SequentialAnimation {
                running: fx.live
                PauseAnimation { duration: out.index * 560 }
                ParallelAnimation {
                    loops: Animation.Infinite
                    ScaleAnimator { target: out; from: 1; to: 1 + 18 * fx.s / out.inner; duration: 1680; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        OpacityAnimator { target: out; from: 0; to: 0.9; duration: 140 }
                        OpacityAnimator { target: out; to: 0; duration: 1540; easing.type: Easing.OutQuad }
                    }
                }
            }
        }
    }
}
