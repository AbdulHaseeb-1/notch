pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// The ring of light round the orb, in eight styles. Only the chosen style is
// built (Loader), every style turns with animators on the render thread and none
// changes a gradient or a path while it runs, so a ring costs the GUI thread
// nothing per frame.
//
// Sized by its parent: the orb's centre is the centre of this item.
Item {
    id: root

    // neon | aurora | comet | orbit | dashes | pulse | prism | halo
    property string style: "neon"
    property real midR: 26            // centre line of the ring, px
    property real thickness: 2.4      // the crisp stroke, px
    property real s: 1                // buddy scale (for the sizes that are not the ring's own)
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    property real speed: 1            // 0.3 .. 2.5
    property bool running: true
    property real glow: 1             // 0 .. 1.5: how strong the soft halo layers are
    // Draw the whole ring in one fragment shader (BuddyShaderRing) instead of the Shape layers below.
    property bool shader: false

    readonly property color c0: Qt.color(stops[0])
    readonly property color c1: Qt.color(stops[1])
    readonly property color c2: Qt.color(stops[2])
    readonly property color c3: Qt.color(stops[3])

    function mix(t) {
        const n = stops.length
        const x = ((t % 1) + 1) % 1 * n
        const i = Math.floor(x) % n
        const f = x - Math.floor(x)
        const a = Qt.color(stops[i])
        const b = Qt.color(stops[(i + 1) % n])
        return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1)
    }

    // A soft halo is a stack of wider and wider, fainter and fainter strokes. Three
    // or four of them show as steps, so this spreads the given (width, alpha)
    // layers into up to a dozen thin ones that follow the same, smoothed, profile.
    // `widths` are in any unit, `unit` is how many px one of them is (it only picks
    // the number of layers); the result is [{ w, o }] in the same unit as `widths`.
    function haloLayers(widths, alphas, unit) {
        let wmax = 0
        for (let k = 0; k < widths.length; ++k)
            wmax = Math.max(wmax, widths[k])
        const n = Math.max(5, Math.min(12, Math.round(wmax * unit / 1.6)))
        const half = wmax / 2
        const smooth = (x) => { const t = Math.max(0, Math.min(1, (x - 0.25) / 0.75)); return 1 - t * t * (3 - 2 * t) }
        const cum = (u) => {
            let c = 0
            for (let k = 0; k < widths.length; ++k)
                c += alphas[k] * smooth(u / (widths[k] / 2))
            return c
        }
        const out = []
        let prev = 0
        for (let j = 0; j < n; ++j) {
            const mid = half * ((n - j) / n + (n - j - 1) / n) / 2
            const c = cum(mid)
            out.push({ w: wmax * (n - j) / n, o: Math.max(0, c - prev) })
            prev = c
        }
        return out
    }

    // Turns whatever is put inside it.
    component Spinner: Item {
        id: spin
        property real period: 6000
        property int dir: 1
        property real from: 0
        anchors.fill: parent
        rotation: from
        RotationAnimator on rotation {
            from: spin.from + (spin.dir > 0 ? 0 : 360)
            to: spin.from + (spin.dir > 0 ? 360 : 0)
            duration: Math.max(400, spin.period / root.speed)
            loops: Animation.Infinite
            running: root.running
        }
    }

    // A glowing arc: wide faint copies under a crisp stroke.
    component Comet: Item {
        id: comet
        property color head: root.c0
        property color mid: root.c1
        property color tail: root.c2
        property real tailLen: 0.6
        property real width_: root.thickness
        property real r: root.midR
        anchors.fill: parent
        Repeater {
            model: root.haloLayers([5.2, 3.0, 1.7], [0.07, 0.14, 0.30], comet.width_)
            BuddyArcRing {
                required property var modelData
                anchors.fill: parent
                midR: comet.r
                thickness: comet.width_ * modelData.w
                head: comet.head
                mid: comet.mid
                tail: comet.tail
                tailLen: comet.tailLen
                opacity: Math.min(1, modelData.o * root.glow)
            }
        }
        BuddyArcRing {
            anchors.fill: parent
            midR: comet.r
            thickness: comet.width_
            head: comet.head
            mid: comet.mid
            tail: comet.tail
            tailLen: comet.tailLen
        }
    }

    // A full ring in the palette, with soft copies.
    component FullRing: Item {
        id: full
        property real r: root.midR
        property real width_: root.thickness
        property var glowWidths: [15, 9.5, 5.5]
        property var glowAlpha: [0.06, 0.12, 0.26]
        property bool crisp: true
        property var palette: root.stops
        anchors.fill: parent
        Repeater {
            model: root.haloLayers(full.glowWidths, full.glowAlpha, root.s)
            BuddyNeonRing {
                required property var modelData
                anchors.fill: parent
                midR: full.r
                thickness: modelData.w * root.s
                stops: full.palette
                opacity: Math.min(1, modelData.o * root.glow)
            }
        }
        BuddyNeonRing {
            visible: full.crisp
            anchors.fill: parent
            midR: full.r
            thickness: full.width_
            stops: full.palette
        }
    }

    Loader {
        anchors.fill: parent
        active: !root.shader
        sourceComponent: {
            switch (root.style) {
            case "aurora": return auroraC
            case "comet": return cometC
            case "orbit": return orbitC
            case "dashes": return dashesC
            case "pulse": return pulseC
            case "prism": return prismC
            case "halo": return haloC
            default: return neonC
            }
        }
    }

    Loader {
        anchors.fill: parent
        active: root.shader
        sourceComponent: BuddyShaderRing {
            style: root.style
            midR: root.midR
            thickness: root.thickness
            s: root.s
            stops: root.stops
            speed: root.speed
            running: root.running
            glow: root.glow
        }
    }

    // ---- neon: the classic conic ring turning slowly --------------------------------
    Component {
        id: neonC
        Spinner {
            period: 9000
            FullRing {}
        }
    }

    // ---- aurora: two coloured streams, one each way ------------------------------------
    Component {
        id: auroraC
        Item {
            FullRing { crisp: false; glowWidths: [11]; glowAlpha: [0.05] }
            Spinner {
                period: 5600
                Comet { head: root.c0; mid: root.c1; tail: root.c1; tailLen: 0.62 }
            }
            Spinner {
                period: 7800
                dir: -1
                from: 180
                Comet { head: root.c2; mid: root.c3; tail: root.c3; tailLen: 0.58 }
            }
            Spinner {
                period: 11000
                from: 90
                Comet { head: root.c1; mid: root.c2; tail: root.c2; tailLen: 0.3; r: root.midR + 4.5 * root.s; width_: root.thickness * 0.55 }
            }
        }
    }

    // ---- comet: one long tail with a bright head ---------------------------------------
    Component {
        id: cometC
        Item {
            BuddyNeonRing {
                anchors.fill: parent
                midR: root.midR
                thickness: Math.max(1, root.thickness * 0.45)
                stops: root.stops
                opacity: 0.16
            }
            Spinner {
                period: 3200
                Comet { head: "#ffffff"; mid: root.c0; tail: root.c1; tailLen: 0.86; width_: root.thickness * 1.15 }
                BuddyBlob {
                    radius: root.thickness * 5.5
                    x: parent.width / 2 - radius
                    y: parent.height / 2 - root.midR - radius
                    color: root.c0
                    a: Math.min(1, 0.55 * root.glow)
                }
                Rectangle {
                    readonly property real d: root.thickness * 2.6
                    width: d
                    height: d
                    radius: d / 2
                    x: parent.width / 2 - d / 2
                    y: parent.height / 2 - root.midR - d / 2
                    color: "#ffffff"
                }
            }
        }
    }

    // ---- orbit: little moons on their own tracks ----------------------------------------
    Component {
        id: orbitC
        Item {
            BuddyNeonRing {
                anchors.fill: parent
                midR: root.midR
                thickness: Math.max(1, root.thickness * 0.5)
                stops: root.stops
                opacity: 0.22
            }
            Repeater {
                model: [
                    { r: 3.0, d: 5.2, period: 5200, dir: 1, from: 20, c: 0 },
                    { r: 8.5, d: 4.2, period: 7600, dir: -1, from: 140, c: 2 },
                    { r: 14.0, d: 3.2, period: 3400, dir: 1, from: 250, c: 1 }
                ]
                Spinner {
                    id: moon
                    required property var modelData
                    period: modelData.period
                    dir: modelData.dir
                    from: modelData.from
                    BuddyBlob {
                        radius: moon.modelData.d * root.s * 2.6
                        x: moon.width / 2 - radius
                        y: moon.height / 2 - (root.midR + moon.modelData.r * root.s) - radius
                        color: root.mix(moon.modelData.c / 4)
                        a: Math.min(1, 0.65 * root.glow)
                    }
                    Rectangle {
                        readonly property real d: moon.modelData.d * root.s
                        width: d
                        height: d
                        radius: d / 2
                        x: moon.width / 2 - d / 2
                        y: moon.height / 2 - (root.midR + moon.modelData.r * root.s) - d / 2
                        color: root.mix(moon.modelData.c / 4)
                    }
                }
            }
        }
    }

    // ---- dashes: a ring of beads with a light running round them -------------------------
    Component {
        id: dashesC
        Item {
            id: dashRing
            readonly property int count: 30
            readonly property real cycle: 2000 / root.speed
            Repeater {
                model: dashRing.count
                Item {
                    id: dash
                    required property int index
                    anchors.centerIn: parent
                    width: 0
                    height: 0
                    rotation: index * 360 / dashRing.count
                    opacity: 0.3
                    Rectangle {
                        readonly property real w: Math.max(1.8, root.thickness * 1.05)
                        x: -w / 2
                        y: -(root.midR + root.thickness * 0.6) - height / 2
                        width: w
                        height: root.thickness * 3.4
                        radius: w / 2
                        color: root.mix(dash.index / dashRing.count)
                    }
                    SequentialAnimation {
                        running: root.running
                        PauseAnimation { duration: dash.index * dashRing.cycle / dashRing.count }
                        SequentialAnimation {
                            loops: Animation.Infinite
                            OpacityAnimator { target: dash; to: 1; duration: 110 }
                            OpacityAnimator { target: dash; to: 0.3; duration: dashRing.cycle * 0.42 }
                            PauseAnimation { duration: Math.max(0, dashRing.cycle * 0.58 - 110) }
                        }
                    }
                }
            }
        }
    }

    // ---- pulse: the ring beats and sends rings outwards ----------------------------------
    Component {
        id: pulseC
        Item {
            id: pulse
            readonly property real cycle: 2400 / root.speed
            BuddyNeonRing {
                anchors.fill: parent
                midR: root.midR
                thickness: root.thickness
                stops: root.stops
            }
            BuddyNeonRing {
                anchors.fill: parent
                midR: root.midR
                thickness: root.thickness * 3.2
                stops: root.stops
                opacity: Math.min(1, 0.16 * root.glow)
            }
            Repeater {
                model: 3
                BuddyNeonRing {
                    id: wave
                    required property int index
                    anchors.fill: parent
                    midR: root.midR + 1.5 * root.s
                    thickness: Math.max(1.2, root.thickness * 0.8)
                    stops: root.stops
                    opacity: 0
                    transformOrigin: Item.Center
                    SequentialAnimation {
                        running: root.running
                        PauseAnimation { duration: wave.index * pulse.cycle / 3 }
                        ParallelAnimation {
                            loops: Animation.Infinite
                            ScaleAnimator { target: wave; from: 1; to: 1.62; duration: pulse.cycle; easing.type: Easing.OutQuad }
                            SequentialAnimation {
                                OpacityAnimator { target: wave; from: 0; to: 0.8; duration: pulse.cycle * 0.12 }
                                OpacityAnimator { target: wave; to: 0; duration: pulse.cycle * 0.88; easing.type: Easing.OutQuad }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- prism: three thin rings that turn against each other -------------------------------
    Component {
        id: prismC
        Item {
            Spinner {
                period: 9000
                FullRing { r: root.midR; width_: root.thickness * 0.8; glowWidths: [8]; glowAlpha: [0.12] }
            }
            Spinner {
                period: 6500
                dir: -1
                FullRing {
                    r: root.midR + 4 * root.s
                    width_: root.thickness * 0.6
                    glowWidths: [6]
                    glowAlpha: [0.10]
                    palette: [root.stops[1], root.stops[2], root.stops[3], root.stops[0]]
                }
            }
            Spinner {
                period: 12500
                FullRing {
                    r: root.midR + 8 * root.s
                    width_: root.thickness * 0.45
                    glowWidths: [5]
                    glowAlpha: [0.08]
                    palette: [root.stops[2], root.stops[3], root.stops[0], root.stops[1]]
                }
            }
        }
    }

    // ---- halo: only the glow, breathing ----------------------------------------------------
    Component {
        id: haloC
        Item {
            id: halo
            transformOrigin: Item.Center
            SequentialAnimation {
                running: root.running
                loops: Animation.Infinite
                ScaleAnimator { target: halo; from: 0.97; to: 1.05; duration: 2400 / root.speed; easing.type: Easing.InOutSine }
                ScaleAnimator { target: halo; from: 1.05; to: 0.97; duration: 2400 / root.speed; easing.type: Easing.InOutSine }
            }
            Spinner {
                period: 16000
                FullRing {
                    crisp: false
                    glowWidths: [30, 21, 14, 8]
                    glowAlpha: [0.05, 0.08, 0.14, 0.24]
                }
            }
        }
    }
}
