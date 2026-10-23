pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// One soft round light: colour in the middle, nothing at the edge. The falloff is
// a gaussian sampled at nine stops, so there is no visible kink or band where
// it meets the dark. The gradient is built once; move, scale or fade the item to
// animate it.
Shape {
    id: blob
    property color color: "white"
    property real a: 0.5
    property real radius: 40
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
            GradientStop { position: 0.0; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 1.000) }
            GradientStop { position: 0.125; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.949) }
            GradientStop { position: 0.25; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.811) }
            GradientStop { position: 0.375; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.622) }
            GradientStop { position: 0.5; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.426) }
            GradientStop { position: 0.625; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.256) }
            GradientStop { position: 0.75; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.130) }
            GradientStop { position: 0.875; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.047) }
            GradientStop { position: 1.0; color: Qt.rgba(blob.color.r, blob.color.g, blob.color.b, blob.a * 0.000) }
        }
        PathAngleArc { centerX: blob.radius; centerY: blob.radius; radiusX: blob.radius; radiusY: blob.radius; startAngle: 0; sweepAngle: 360 }
    }
}
