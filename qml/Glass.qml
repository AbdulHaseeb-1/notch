import QtQuick

// Frosted-glass surface for the notch, its flyouts and menus. On Windows the
// blur behind the window comes from the system acrylic that GlassBackdrop
// sets up (clipped to the visible shapes by the window mask). This item adds a
// light tint, a top sheen and a rim light. Children are drawn above the glass.
Item {
    id: glass

    property real radius: 22
    property color tint: "#101217"
    property color edge: "#2b2f33"

    readonly property bool frosted: glassBackdrop.blurActive

    // Tint: light when the blur is there, denser when it is not, so text
    // stays readable over any wallpaper.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: Qt.rgba(glass.tint.r, glass.tint.g, glass.tint.b, glass.frosted ? 0.2 : 0.72)
        Behavior on color { ColorAnimation { duration: 200 } }
    }

    // Sheen: a soft highlight that fades out down the surface.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#24ffffff" }
            GradientStop { position: 0.45; color: "#07ffffff" }
            GradientStop { position: 1.0; color: "#00ffffff" }
        }
    }

    // Rim light: the hairline edge.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 1
        border.color: glass.edge
        Behavior on border.color { ColorAnimation { duration: 180 } }
    }
}
