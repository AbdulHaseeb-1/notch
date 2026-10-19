pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Shape {
    id: ring
    property real midR: 30          // centre line of the stroke
    property real thickness: 2
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    readonly property real outerR: midR + thickness / 2
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeColor: "transparent"
        strokeWidth: -1
        fillRule: ShapePath.OddEvenFill
        fillGradient: ConicalGradient {
            centerX: ring.width / 2
            centerY: ring.height / 2
            angle: 90
            GradientStop { position: 0.00; color: ring.stops[0] }
            GradientStop { position: 0.27; color: ring.stops[1] }
            GradientStop { position: 0.52; color: ring.stops[2] }
            GradientStop { position: 0.76; color: ring.stops[3] }
            GradientStop { position: 1.00; color: ring.stops[0] }
        }
        PathAngleArc {
            centerX: ring.width / 2
            centerY: ring.height / 2
            radiusX: ring.outerR
            radiusY: ring.outerR
            startAngle: 0
            sweepAngle: 360
        }
        PathAngleArc {
            moveToStart: true
            centerX: ring.width / 2
            centerY: ring.height / 2
            radiusX: ring.outerR - ring.thickness
            radiusY: ring.outerR - ring.thickness
            startAngle: 0
            sweepAngle: 360
        }
    }
}
