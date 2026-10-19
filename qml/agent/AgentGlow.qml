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

    readonly property real depth: 16 + 44 * intensity
    // Warm stone: the accent pulled towards the theme's amber so the edge
    // reads on light and dark wallpapers alike.
    readonly property color tint: Qt.tint(Theme.accent, Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.55))
    readonly property color warm: Theme.warning

    Item {
        id: frame
        anchors.fill: parent
        opacity: glow.engaged ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }

        property real pulse
        SequentialAnimation on pulse {
            running: glow.visible
            loops: Animation.Infinite
            NumberAnimation { to: 1; duration: 1500; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 1500; easing.type: Easing.InOutSine }
        }
        property real sweep
        NumberAnimation on sweep {
            running: glow.visible
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 7000
        }

        readonly property real edgeAlpha: (0.28 + 0.50 * glow.intensity) * (0.72 + 0.28 * pulse)

        // Four feathered edges. Each fades from the screen edge inwards.
        Rectangle {
            width: parent.width
            height: glow.depth
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha) }
                GradientStop { position: 0.45; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha * 0.3) }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            y: parent.height - height
            width: parent.width
            height: glow.depth
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.55; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha * 0.3) }
                GradientStop { position: 1; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha) }
            }
        }
        Rectangle {
            width: glow.depth
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha) }
                GradientStop { position: 0.45; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha * 0.3) }
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
                GradientStop { position: 0.55; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha * 0.3) }
                GradientStop { position: 1; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, frame.edgeAlpha) }
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
                    GradientStop { position: 0.00; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0.95 * glow.intensity + 0.05) }
                    GradientStop { position: 0.12; color: Qt.rgba(glow.warm.r, glow.warm.g, glow.warm.b, 0.70 * glow.intensity) }
                    GradientStop { position: 0.30; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0.30 * glow.intensity) }
                    GradientStop { position: 0.70; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0.30 * glow.intensity) }
                    GradientStop { position: 0.88; color: Qt.rgba(glow.warm.r, glow.warm.g, glow.warm.b, 0.55 * glow.intensity) }
                    GradientStop { position: 1.00; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0.95 * glow.intensity + 0.05) }
                }
                PathRectangle { x: 0; y: 0; width: frame.width; height: frame.height }
                PathRectangle { x: 3; y: 3; width: frame.width - 6; height: frame.height - 6; radius: 10 }
            }
        }
    }

    Component.onCompleted: AgentControl.excludeFromCapture(glow)
}
