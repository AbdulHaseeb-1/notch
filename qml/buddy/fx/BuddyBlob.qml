pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// One soft round light: colour in the middle, nothing at the edge. The gradient
// is built once; move, scale or fade the item to animate it.
Shape {
    id: blob
    property color color: "white"
    property real a: 0.5
    property real radius: 40
    property real core: 0.42            // how much of the centre colour holds before it fades
    width: radius * 2
    height: radius * 2
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeColor: "transparent"
        fillGradient: RadialGradient {
            centerX: blob.radius
            centerY: blob.radius
            centerRadius: blob.radius
            focalX: centerX
            focalY: centerY
            GradientStop { position: 0.0; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a) }
            GradientStop { position: blob.core; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.42) }
            GradientStop { position: 1.0; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, 0) }
        }
        PathAngleArc { centerX: blob.radius; centerY: blob.radius; radiusX: blob.radius; radiusY: blob.radius; startAngle: 0; sweepAngle: 360 }
    }
}
