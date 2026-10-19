pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: rs
    property var option: ({})
    property bool selected: false
    readonly property var stops: rs.option.colors && rs.option.colors.length === 4
                                 ? rs.option.colors : ["#ffffff", "#ffffff", "#ffffff", "#ffffff"]
    signal picked()
    width: 70
    height: 74

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 2
        width: 52
        height: 52
        radius: 26
        color: rs.selected || rsHit.containsMouse ? Theme.hover : "transparent"
        border.width: rs.selected ? 1 : 0
        border.color: Theme.borderStrong
        Behavior on color { ColorAnimation { duration: 140 } }
    }

    Item {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: 40
        height: 40
        scale: rsHit.pressed ? 0.92 : (rsHit.containsMouse ? 1.06 : 1)
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }

        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 32
            radius: 16
            color: Theme.orb
            Row {
                anchors.centerIn: parent
                spacing: 5
                Repeater {
                    model: 2
                    Rectangle { width: 4; height: 9; radius: 2; color: "#ffffff" }
                }
            }
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            rotation: rsHit.containsMouse ? 120 : 0
            Behavior on rotation { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
            ShapePath {
                strokeColor: "transparent"
                strokeWidth: -1
                fillRule: ShapePath.OddEvenFill
                fillGradient: ConicalGradient {
                    centerX: 20
                    centerY: 20
                    angle: 90
                    GradientStop { position: 0.00; color: rs.stops[0] }
                    GradientStop { position: 0.27; color: rs.stops[1] }
                    GradientStop { position: 0.52; color: rs.stops[2] }
                    GradientStop { position: 0.76; color: rs.stops[3] }
                    GradientStop { position: 1.00; color: rs.stops[0] }
                }
                PathAngleArc { centerX: 20; centerY: 20; radiusX: 18.5; radiusY: 18.5; startAngle: 0; sweepAngle: 360 }
                PathAngleArc { moveToStart: true; centerX: 20; centerY: 20; radiusX: 15.5; radiusY: 15.5; startAngle: 0; sweepAngle: 360 }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 58
        text: rs.option.label || ""
        color: rs.selected ? Theme.ink : Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: rs.selected ? Font.DemiBold : Font.Medium
    }
    MouseArea {
        id: rsHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.RadioButton
        Accessible.name: rs.option.label || ""
        Accessible.checked: rs.selected
        onClicked: rs.picked()
    }
}
