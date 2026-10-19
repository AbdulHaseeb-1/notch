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

    readonly property real level: strength * Math.max(always ? 0.34 : 0, activity)
    opacity: Math.min(1, level * 1.15)
    visible: mode !== "off" && opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

    // ---- bloom: light all round, colours drifting ------------------------------------
    Item {
        id: bloom
        visible: glow.mode === "bloom"
        anchors.centerIn: parent
        width: glow.reach * 2
        height: width

        // The base: a ring of light hugging the orb.
        Shape {
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
                    GradientStop { position: Math.min(0.98, (glow.orbRadius + 2 * glow.s) / glow.reach); color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.62) }
                    GradientStop { position: 0.72; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0.14) }
                    GradientStop { position: 1.0; color: Qt.rgba(glow.c1.r, glow.c1.g, glow.c1.b, 0) }
                }
                PathAngleArc { centerX: bloom.width / 2; centerY: bloom.height / 2; radiusX: glow.reach; radiusY: glow.reach; startAngle: 0; sweepAngle: 360 }
            }
        }

        // Two lights that circle the orb, so the colour moves round it.
        Item {
            anchors.fill: parent
            RotationAnimator on rotation {
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
        visible: glow.mode === "floor"
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
