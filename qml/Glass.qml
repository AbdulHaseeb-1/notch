import QtQuick

// Frosted-glass surface for the notch, its flyouts and menus. On Windows the
// blur behind the window is the system acrylic that GlassBackdrop sets up,
// clipped to the visible shapes by the window mask. On top of it this draws:
//   - the body: the tint, a little lighter at the top and deeper at the bottom,
//     which reads as the thickness of a pane of glass;
//   - a specular sheen across the upper part and a soft shade at the bottom;
//   - a bright hairline along the top edge, an inner rim and the outer edge.
// Children draw above all of it.
Item {
    id: glass

    Theme { id: t }

    property real radius: 22
    property color tint: t.surface
    property color edge: t.border

    readonly property bool frosted: glassBackdrop.blurActive
    readonly property real base: frosted ? t.glassFrosted : t.glassSolid

    function fill(delta) {
        return Qt.rgba(glass.tint.r, glass.tint.g, glass.tint.b,
                       Math.max(0, Math.min(1, glass.base + delta)))
    }

    // Body.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: glass.fill(0.06) }
            GradientStop { position: 1.0; color: glass.fill(-0.04) }
        }
    }

    // Specular sheen, fading out by the middle of the surface.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: t.sheen }
            GradientStop { position: 0.5; color: "transparent" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Soft shade across the lower half, for depth.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height / 2
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: t.shade }
        }
    }

    // Bright hairline along the top edge, brightest in the middle.
    Rectangle {
        x: glass.radius * 0.6
        y: 1
        width: glass.width - 2 * x
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: t.hairline }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Inner rim, one pixel in from the edge.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, glass.radius - 1)
        color: "transparent"
        border.width: 1
        border.color: t.hairline
    }

    // Outer edge.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 1
        border.color: glass.edge
        Behavior on border.color { ColorAnimation { duration: 180 } }
    }
}
