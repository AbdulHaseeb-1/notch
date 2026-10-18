import QtQuick

// Solid, opaque surface for the notch, its pills, flyouts and menus.
// Replaces the old translucent glass/sheen/rim-light effect with a crisp,
// lightweight flat surface and subtle border.
Item {
    id: glass


    property real radius: 22
    property color tint: Theme.surface
    property color edge: "transparent"
    property bool shadowEnabled: false

    readonly property real r: Math.max(0, Math.min(radius, width / 2, height / 2))

    Rectangle {
        anchors.fill: parent
        radius: glass.r
        color: Qt.rgba(glass.tint.r, glass.tint.g, glass.tint.b, 1.0)
        border.width: glass.edge.a > 0 ? 1.5 : 1
        border.color: glass.edge.a > 0 ? glass.edge : Theme.border
        Behavior on border.color { ColorAnimation { duration: 150 } }
    }
}
