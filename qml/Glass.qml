import QtQuick

// Dark stone surface for the notch, its pills, flyouts and menus: one
// near-opaque rectangle (Theme.surfaceAlpha) with a faint white hairline, so
// panels read as smoked glass over the desktop without any blur, layer or
// shader cost.
Item {
    id: glass


    property real radius: Theme.radiusXl
    property color tint: Theme.surface
    property color edge: "transparent"
    property bool shadowEnabled: false

    readonly property real r: Math.max(0, Math.min(radius, width / 2, height / 2))

    Rectangle {
        anchors.fill: parent
        radius: glass.r
        color: Qt.rgba(glass.tint.r, glass.tint.g, glass.tint.b, Theme.surfaceAlpha)
        border.width: glass.edge.a > 0 ? 1.5 : 1
        border.color: glass.edge.a > 0 ? glass.edge : Theme.glassBorderStrong
        Behavior on border.color { ColorAnimation { duration: 150 } }
    }
}
