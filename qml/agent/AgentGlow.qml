import QtQuick
import QtQuick.Window
import QtQuick.Shapes

// Soft glow around the edges of one screen while Buddy is in control.
// A click-through, transparent window: it never takes focus or input, so the
// user can always reach what is underneath (and take over with the real
// mouse, which pauses the agent).
Window {
    id: glow

    required property var targetScreen
    property bool engaged: false
    property real intensity: 0.6
    // soft | prism | pulse | chase | aurora (Settings, Buddy, Look & glow)
    property string style: "soft"

    // Window.screen takes a ScreenInfo from Application.screens (never a raw
    // QScreen*: since Qt 6.12 assigning one crashes the write path). The
    // wrapped QScreen here is only used for its geometry, so match on that.
    readonly property QtObject glowScreen: {
        const list = Application.screens
        if (!targetScreen || list.length === 0)
            return null
        for (let i = 0; i < list.length; ++i) {
            const g = targetScreen.geometry
            if (list[i].virtualX === g.x && list[i].virtualY === g.y
                    && list[i].width === g.width && list[i].height === g.height)
                return list[i]
        }
        return list[0]
    }
    screen: glowScreen
    x: targetScreen ? targetScreen.geometry.x : 0
    y: targetScreen ? targetScreen.geometry.y : 0
    width: targetScreen ? targetScreen.geometry.width : 640
    height: targetScreen ? targetScreen.geometry.height : 480
    color: "transparent"
    title: "Noch agent glow"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus | Qt.NoDropShadowWindowHint
    visible: engaged || frame.opacity > 0.01

    readonly property real depth: (16 + 44 * intensity) * (style === "pulse" ? 1.3 : (style === "chase" ? 0.7 : 1))
    // Warm stone: the accent pulled towards the theme's amber so the edge
    // reads on light and dark wallpapers alike.
    readonly property color tint: Qt.tint(Theme.accent, Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.55))
    readonly property color warm: Theme.warning

    // The palette Buddy's ring uses, for the styles that borrow its colours.
    readonly property var palette: SystemAppearance.ringColors
    readonly property bool coloured: style === "prism" || style === "aurora"
    readonly property real chaseTail: style === "chase" ? 0.32 : 0.30
    readonly property real k: intensity
    readonly property color rimA: style === "chase" ? Qt.rgba(1, 1, 1, 0.95)
        : (coloured ? Qt.rgba(eTop.r, eTop.g, eTop.b, 0.95 * k + 0.05) : Qt.rgba(tint.r, tint.g, tint.b, 0.95 * k + 0.05))
    readonly property color rimB: style === "chase" ? Qt.rgba(tint.r, tint.g, tint.b, 0.7 * k)
        : (coloured ? Qt.rgba(eRight.r, eRight.g, eRight.b, 0.85 * k) : Qt.rgba(warm.r, warm.g, warm.b, 0.70 * k))
    readonly property color rimC: style === "chase" ? Qt.rgba(tint.r, tint.g, tint.b, 0.0)
        : (coloured ? Qt.rgba(eBottom.r, eBottom.g, eBottom.b, 0.85 * k) : Qt.rgba(tint.r, tint.g, tint.b, 0.30 * k))
    readonly property color rimD: style === "chase" ? Qt.rgba(tint.r, tint.g, tint.b, 0.0)
        : (coloured ? Qt.rgba(eLeft.r, eLeft.g, eLeft.b, 0.85 * k) : Qt.rgba(tint.r, tint.g, tint.b, 0.30 * k))
    readonly property color rimE: style === "chase" ? Qt.rgba(tint.r, tint.g, tint.b, 0.0)
        : (coloured ? Qt.rgba(eTop.r, eTop.g, eTop.b, 0.7 * k) : Qt.rgba(warm.r, warm.g, warm.b, 0.55 * k))

    // One colour per edge (top, right, bottom, left).
    readonly property color eTop: coloured ? Qt.color(palette[0]) : tint
    readonly property color eRight: coloured ? Qt.color(palette[1]) : tint
    readonly property color eBottom: coloured ? Qt.color(palette[2]) : tint
    readonly property color eLeft: coloured ? Qt.color(palette[3]) : tint
    // How much the edges breathe.
    readonly property real breathe: style === "pulse" ? 0.55 : (style === "aurora" ? 0.4 : (style === "chase" ? 0.12 : 0.28))
    readonly property int breathMs: style === "pulse" ? 1100 : (style === "aurora" ? 2600 : 1500)

    Item {
        id: frame
        anchors.fill: parent
        opacity: glow.engaged ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }

        property real pulse
        SequentialAnimation on pulse {
            running: glow.visible
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: glow.breathMs; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: glow.breathMs; easing.type: Easing.InOutSine }
        }
        property real sweep
        NumberAnimation on sweep {
            running: glow.visible
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: glow.style === "chase" ? 2600 : (glow.style === "aurora" ? 11000 : 7000)
        }

        readonly property real baseAlpha: (0.28 + 0.50 * glow.intensity) * (glow.style === "chase" ? 0.55 : 1)
        readonly property real edgeAlpha: baseAlpha * (1 - glow.breathe + glow.breathe * pulse)
        // Aurora: the top and bottom breathe against the sides, so the light seems to travel.
        readonly property real sideAlpha: glow.style === "aurora"
                                          ? baseAlpha * (1 - glow.breathe + glow.breathe * (1 - pulse)) : edgeAlpha

        // Four feathered edges. Each fades from the screen edge inwards.
        // The gradient stops are constant (a changing stop colour makes Qt
        // build and cache a new gradient texture on every frame); the pulse
        // only drives the opacity of the group.
        Item {
            anchors.fill: parent
            opacity: frame.edgeAlpha

            Rectangle {
                width: parent.width
                height: glow.depth
                gradient: Gradient {
                    GradientStop { position: 0; color: glow.eTop }
                    GradientStop { position: 0.45; color: Qt.rgba(glow.eTop.r, glow.eTop.g, glow.eTop.b, 0.3) }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                y: parent.height - height
                width: parent.width
                height: glow.depth
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.55; color: Qt.rgba(glow.eBottom.r, glow.eBottom.g, glow.eBottom.b, 0.3) }
                    GradientStop { position: 1; color: glow.eBottom }
                }
            }
        }
        Item {
            anchors.fill: parent
            opacity: frame.sideAlpha

            Rectangle {
                width: glow.depth
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: glow.eLeft }
                    GradientStop { position: 0.45; color: Qt.rgba(glow.eLeft.r, glow.eLeft.g, glow.eLeft.b, 0.3) }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                x: parent.width - width
                width: glow.depth
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.55; color: Qt.rgba(glow.eRight.r, glow.eRight.g, glow.eRight.b, 0.3) }
                    GradientStop { position: 1; color: glow.eRight }
                }
            }
        }

        // A crisp rim with a slow highlight running around the screen.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillRule: ShapePath.OddEvenFill
                strokeColor: "transparent"
                fillGradient: ConicalGradient {
                    centerX: frame.width / 2
                    centerY: frame.height / 2
                    angle: frame.sweep
                    // soft / pulse: the warm stone highlight. prism / aurora: the ring palette all the
                    // way round. chase: one bright streak with a short tail.
                    GradientStop { position: 0.00; color: glow.rimA }
                    GradientStop { position: 0.12; color: glow.rimB }
                    GradientStop { position: glow.chaseTail; color: glow.rimC }
                    GradientStop { position: 0.70; color: glow.rimD }
                    GradientStop { position: 0.88; color: glow.rimE }
                    GradientStop { position: 1.00; color: glow.rimA }
                }
                PathRectangle { x: 0; y: 0; width: frame.width; height: frame.height }
                PathRectangle { x: 3; y: 3; width: frame.width - 6; height: frame.height - 6; radius: 10 }
            }
        }
    }

    Component.onCompleted: AgentControl.excludeFromCapture(glow)
}
