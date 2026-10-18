import QtQuick
import QtQuick.Shapes

// Vector glyph icons on a 24-unit grid with support for stroked and filled
// icons, drawn with the curve renderer so they stay razor sharp at any size
// and display scale. The stroke width is in device-independent pixels, so a
// 13 px icon and a 24 px icon read with the same line weight.
Item {
    id: glyph
    property string name: "close"

    property color color: Theme.text
    property real size: 16
    property real stroke: 1.8
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    // Icon geometry lives once in the GlyphPaths singleton instead of in
    // every Glyph instance.
    readonly property bool isFill: filled || !!GlyphPaths.fillIcons[glyph.name]

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: glyph.width / 24; yScale: glyph.height / 24 }

        ShapePath {
            strokeColor: glyph.isFill ? "transparent" : glyph.color
            strokeWidth: glyph.isFill ? 0 : glyph.stroke * 24 / Math.max(1, glyph.width)
            fillColor: glyph.isFill ? glyph.color : "transparent"
            fillRule: ShapePath.WindingFill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: GlyphPaths.paths[glyph.name] || "" }
        }
    }
}
