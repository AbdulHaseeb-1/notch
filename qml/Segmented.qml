import QtQuick

// Segmented control with a sliding white highlight.
Item {
    id: seg
    property var options: []          // [{ label, value }]
    property var value
    signal picked(var value)

    Theme { id: t }

    readonly property int currentIndex: {
        for (let i = 0; i < options.length; ++i) {
            const ov = options[i].value
            if (typeof ov === "string" || typeof value === "string") {
                if (String(ov) === String(value))
                    return i
            } else if (Math.abs(Number(ov) - Number(value)) < 0.001)
                return i
        }
        return -1
    }
    readonly property real cellW: options.length > 0 ? (width - 6) / options.length : 0

    implicitWidth: 240
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: t.raised
        border.color: t.borderSoft
        border.width: 1
    }

    Rectangle {
        id: highlight
        visible: seg.currentIndex >= 0
        y: 3
        height: parent.height - 6
        width: seg.cellW
        x: 3 + Math.max(0, seg.currentIndex) * seg.cellW
        radius: height / 2
        color: t.accent
        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutQuint } }
        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutQuint } }
    }

    Row {
        x: 3
        y: 3
        Repeater {
            model: seg.options
            delegate: Item {
                id: cell
                required property int index
                required property var modelData
                width: seg.cellW
                height: seg.height - 6

                readonly property bool selected: index === seg.currentIndex

                Text {
                    anchors.centerIn: parent
                    text: cell.modelData.label
                    color: cell.selected ? t.inverse : (cellHit.containsMouse ? t.text : t.textDim)
                    font.family: t.font
                    font.pixelSize: 12
                    font.weight: cell.selected ? Font.Bold : Font.Medium
                    Behavior on color { ColorAnimation { duration: 160 } }
                }

                MouseArea {
                    id: cellHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.picked(cell.modelData.value)
                }
            }
        }
    }
}
