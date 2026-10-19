pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// A ring whose colour fades along its length: a bright head with a tail that
// thins out behind it (a comet laid round the orb). Spin it with a
// RotationAnimator; the geometry is built once. The head sits at 12 o'clock and
// the tail trails anti-clockwise, so a clockwise spin drags it behind.
Shape {
    id: ring
    property real midR: 30
    property real thickness: 2
    property color head: "#3ee8ff"
    property color mid: "#8b6cff"
    property color tail: "#ff4fd8"
    property real tailLen: 0.6          // how much of the circle the tail covers (0.1 .. 1)
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
            GradientStop { position: 0.000; color: Qt.rgba(ring.head.r, ring.head.g, ring.head.b, 0) }
            GradientStop { position: 0.010; color: ring.head }
            GradientStop { position: ring.tailLen * 0.42; color: Qt.rgba(ring.mid.r, ring.mid.g, ring.mid.b, 0.72) }
            GradientStop { position: ring.tailLen; color: Qt.rgba(ring.tail.r, ring.tail.g, ring.tail.b, 0) }
            GradientStop { position: 1.000; color: Qt.rgba(ring.tail.r, ring.tail.g, ring.tail.b, 0) }
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
