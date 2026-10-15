import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// The shared glass surface for the notch, its pills, flyouts and menus.
// Built the way Apple builds its materials: few layers, each doing one job.
//   1. a soft, tight shadow that lifts the surface off the desktop;
//   2. the body, a single tinted fill with a faint lift toward the top;
//   3. light falling across the upper part (the sheen);
//   4. a 1px rim that is lit from above - bright at the top edge, quiet down
//      the sides - drawn as a true gradient ring so curves stay crisp.
// With "Blur behind" on, the system blur shows through a thinner body; with
// it off the body is dense enough to read on any wallpaper.
// Children draw above all of it.
Item {
    id: glass

    Theme { id: t }

    property real radius: 22
    property color tint: t.surface
    // Optional outline colour for emphasis (drag-over, active); none by default.
    property color edge: "transparent"
    property bool shadowEnabled: true

    readonly property bool frosted: glassBackdrop.blurActive
    readonly property real base: frosted ? t.glassFrosted : t.glassSolid
    readonly property real r: Math.max(0, Math.min(radius, width / 2, height / 2))

    function fill(lift) {
        // Mix a little white (dark glass) or black (light glass) into the tint.
        const k = t.light ? 0 : lift
        return Qt.rgba(Math.min(1, glass.tint.r + k), Math.min(1, glass.tint.g + k),
                       Math.min(1, glass.tint.b + k), glass.base)
    }

    // Rounded-rect outline as an SVG sub-path, inset by `i` pixels.
    function rrect(i) {
        const w = glass.width - 2 * i
        const h = glass.height - 2 * i
        const rr = Math.max(0, glass.r - i)
        if (w <= 0 || h <= 0)
            return ""
        return "M " + (i + rr) + " " + i
             + " H " + (i + w - rr) + " A " + rr + " " + rr + " 0 0 1 " + (i + w) + " " + (i + rr)
             + " V " + (i + h - rr) + " A " + rr + " " + rr + " 0 0 1 " + (i + w - rr) + " " + (i + h)
             + " H " + (i + rr) + " A " + rr + " " + rr + " 0 0 1 " + i + " " + (i + h - rr)
             + " V " + (i + rr) + " A " + rr + " " + rr + " 0 0 1 " + (i + rr) + " " + i + " Z"
    }

    // 1. Shadow.
    RectangularShadow {
        visible: glass.shadowEnabled && glass.width > 0
        anchors.fill: parent
        offset: Qt.vector2d(0, 4)
        radius: glass.r
        blur: 18
        spread: -1
        color: t.shadow
        cached: true
    }

    // 2. Body.
    Rectangle {
        anchors.fill: parent
        radius: glass.r
        gradient: Gradient {
            GradientStop { position: 0.0; color: glass.fill(0.035) }
            GradientStop { position: 1.0; color: glass.fill(0) }
        }
    }

    // 3. Sheen: light across the top of the surface, gone by the middle.
    Rectangle {
        width: parent.width
        height: Math.min(parent.height * 0.55, 96)
        radius: glass.r
        gradient: Gradient {
            GradientStop { position: 0.0; color: t.sheen }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // 4. Rim light.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: -1
            fillRule: ShapePath.OddEvenFill
            fillGradient: LinearGradient {
                x1: 0; y1: 0
                x2: 0; y2: Math.max(1, glass.height)
                GradientStop { position: 0.0; color: t.rimTop }
                GradientStop { position: Math.min(0.5, 30 / Math.max(1, glass.height)); color: t.rimMid }
                GradientStop { position: 1 - Math.min(0.4, 24 / Math.max(1, glass.height)); color: t.rimMid }
                GradientStop { position: 1.0; color: t.rimBottom }
            }
            PathSvg { path: glass.rrect(0) + " " + glass.rrect(1) }
        }
    }

    // Emphasis outline (only when a caller asks for one).
    Rectangle {
        anchors.fill: parent
        radius: glass.r
        color: "transparent"
        border.width: glass.edge.a > 0 ? 1.5 : 0
        border.color: glass.edge
        opacity: glass.edge.a > 0 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on border.color { ColorAnimation { duration: 160 } }
    }
}
