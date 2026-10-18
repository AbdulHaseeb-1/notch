import QtQuick
import QtQuick.Shapes

// One SVG icon from qml/icons/*.svg (24-unit grid, Lucide-derived, round caps
// and joins). IconSet parses the files once; this item only looks the path
// up and draws it with a Shape so it stays vector-sharp at any size and DPI
// and recolours for free. The stroke is in device-independent pixels, so a
// 13 px and a 20 px icon share the same line weight.
Item {
    id: glyph
    property string name: "close"

    property color color: Theme.text
    property real size: Theme.iconMd
    property real stroke: Theme.iconStroke
    // Force a solid fill (the SVG's own fill="currentColor" also enables it).
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    readonly property string pathData: Icons.path(glyph.name)
    readonly property bool isFill: filled || Icons.isFilled(glyph.name)

    Shape {
        anchors.fill: parent
        visible: glyph.pathData.length > 0
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: glyph.width / 24; yScale: glyph.height / 24 }

        ShapePath {
            strokeColor: glyph.color
            strokeWidth: glyph.stroke * 24 / Math.max(1, glyph.width)
            fillColor: glyph.isFill ? glyph.color : "transparent"
            fillRule: ShapePath.WindingFill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: glyph.pathData }
        }
    }
}
