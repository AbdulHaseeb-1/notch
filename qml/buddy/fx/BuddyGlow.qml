pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// A coloured light spilling from the orb: a soft bloom all round it, or a pool of
// light on the "floor" under it. Built from radial gradients whose stops never
// change, so showing it costs a fill and nothing else; the colours drift slowly
// by turning the whole thing.
Item {
    id: glow

    property real orbRadius: 26
    property real s: 1
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    property string mode: "bloom"      // bloom | floor | off
    property real strength: 0.6        // 0 .. 1
    property real activity: 0          // 0 .. 1: how much is going on (busy, happy, speaking ...)
    property bool always: false        // faintly lit even when nothing is going on
    property bool running: true

    // How far the light reaches from the orb's centre. The click-through mask
    // leaves room for exactly this much (see Buddy.fxReach).
    readonly property real reach: orbRadius + 34 * s
    readonly property color c0: Qt.color(stops[0])
    readonly property color c1: Qt.color(stops[1])
    readonly property color c2: Qt.color(stops[2])
    readonly property color c3: Qt.color(stops[3])

    // The farthest the light reaches from the orb's centre, whatever the mode: the
    // bloom is a disc, the floor pool a squashed one that sits low, so its corners
    // go farther than the bloom's disc. The click-through mask has to cover this,
    // or the window region clips the light into a hard, jagged edge.
    readonly property real extent: {
        if (drawn !== "floor")
            return reach
        const a = reach
        const b = reach * 0.26
        const d = orbRadius * 1.28
        const t = Math.max(-1, Math.min(1, d * b / (a * a - b * b)))
        return Math.sqrt(a * a + d * d + 2 * d * b * t - (a * a - b * b) * t * t)
    }

    // The mode being drawn. Switching to "off" fades the light out instead of
    // dropping it, so this keeps the last real mode until the fade is done.
    property string drawn: mode
    onModeChanged: if (mode !== "off") drawn = mode

    readonly property real level: mode === "off" ? 0 : strength * Math.max(always ? 0.34 : 0, activity)
    // The big light (bloom or floor pool) fades with the activity.
    property real bodyOpacity: Math.min(1, level * 1.15)
    Behavior on bodyOpacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    // True from the moment the light starts to show until it has fully faded out.
    // The window mask follows this, never the activity that drives it, so the
    // mask cannot shrink and cut the light off while it is still fading.
    readonly property bool lit: drawn !== "off" && bodyOpacity > 0.01

    // A thin rim of light that hugs the orb and stays on whenever the glow is not
    // "off", busy or not: the orb is near-black, so on a dark window or wallpaper
    // it would otherwise melt into the background.
    property real rimOpacity: mode === "off" ? 0 : Math.min(1, 0.32 + 0.45 * strength)
    Behavior on rimOpacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    readonly property bool rimLit: rimOpacity > 0.01
    readonly property real rimExtent: orbRadius + 9 * s

    visible: lit || rimLit

    Item {
        id: rim
        visible: glow.rimLit
        opacity: glow.rimOpacity
        anchors.centerIn: parent
        width: glow.rimExtent * 2
        height: width
        Shape {
            id: rimShape
            readonly property real p1: Math.min(0.98, (glow.orbRadius + 1.5 * glow.s) / glow.rimExtent)
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: rim.width / 2
                    centerY: rim.height / 2
                    centerRadius: glow.rimExtent
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: Math.max(0.01, (glow.orbRadius - 2 * glow.s) / glow.rimExtent); color: "transparent" }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.700) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0.15; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.639) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0.3; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.484) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0.5; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.249) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0.7; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.087) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 0.85; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.027) }
                    GradientStop { position: rimShape.p1 + (1 - rimShape.p1) * 1.0; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.000) }
                }
                PathAngleArc { centerX: rim.width / 2; centerY: rim.height / 2; radiusX: glow.rimExtent; radiusY: glow.rimExtent; startAngle: 0; sweepAngle: 360 }
            }
        }
    }

    // ---- bloom: light all round, colours drifting ------------------------------------
    Item {
        id: bloom
        visible: glow.lit && glow.drawn === "bloom"
        opacity: glow.bodyOpacity
        anchors.centerIn: parent
        width: glow.reach * 2
        height: width

        // The base: a ring of light hugging the orb, fading out like a gaussian
        // (a straight ramp shows a kink where it changes slope).
        Shape {
            id: bloomShape
            readonly property real p1: Math.min(0.98, (glow.orbRadius + 2 * glow.s) / glow.reach)
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: bloom.width / 2
                    centerY: bloom.height / 2
                    centerRadius: glow.reach
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: Math.max(0.01, (glow.orbRadius - 2 * glow.s) / glow.reach); color: "transparent" }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.620) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.1; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.590) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.2; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.507) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.35; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.334) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.5; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.175) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.7; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.050) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 0.85; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.013) }
                    GradientStop { position: bloomShape.p1 + (1 - bloomShape.p1) * 1.0; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.000) }
                }
                PathAngleArc { centerX: bloom.width / 2; centerY: bloom.height / 2; radiusX: glow.reach; radiusY: glow.reach; startAngle: 0; sweepAngle: 360 }
            }
        }

        // Two lights that circle the orb, so the colour moves round it.
        Item {
            anchors.fill: parent
            // A NumberAnimation, not a RotationAnimator: measured on this machine, the
            // render-thread animator costs about twice the CPU and GPU for the same motion.
            NumberAnimation on rotation {
                from: 0
                to: 360
                duration: 14000
                loops: Animation.Infinite
                running: glow.running && glow.visible
            }
            BuddyBlob {
                color: glow.c0
                a: 0.52
                radius: glow.reach * 0.62
                x: bloom.width / 2 - radius
                y: bloom.height / 2 - glow.orbRadius * 0.95 - radius * 0.62
            }
            BuddyBlob {
                color: glow.c2
                a: 0.46
                radius: glow.reach * 0.6
                x: bloom.width / 2 - radius
                y: bloom.height / 2 + glow.orbRadius * 0.95 - radius * 1.38
            }
            BuddyBlob {
                color: glow.c3
                a: 0.30
                radius: glow.reach * 0.46
                x: bloom.width / 2 + glow.orbRadius * 0.9 - radius
                y: bloom.height / 2 - radius
            }
        }
    }

    // ---- floor: a pool of light under the floating orb ------------------------------------
    Item {
        id: pool
        visible: glow.lit && glow.drawn === "floor"
        opacity: glow.bodyOpacity
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: glow.orbRadius * 1.28
        width: glow.reach * 2
        height: width
        transform: Scale { origin.x: pool.width / 2; origin.y: pool.height / 2; yScale: 0.26 }
        BuddyBlob {
            color: glow.c1
            a: 0.85
            radius: glow.reach
            x: 0
            y: 0
        }
        BuddyBlob {
            color: glow.c0
            a: 0.7
            radius: glow.reach * 0.55
            x: pool.width / 2 - radius - glow.orbRadius * 0.45
            y: pool.height / 2 - radius
        }
        BuddyBlob {
            color: glow.c2
            a: 0.6
            radius: glow.reach * 0.5
            x: pool.width / 2 - radius + glow.orbRadius * 0.5
            y: pool.height / 2 - radius
        }
    }
}
