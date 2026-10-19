pragma ComponentBehavior: Bound
import QtQuick

Column {
    id: ls
    property string label: ""
    property real value: 0
    property int decimals: 2
    signal moved(real value)
    width: parent ? parent.width : 200
    spacing: 6

    Item {
        width: parent.width
        height: 15
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: ls.label
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: Number(ls.value).toFixed(ls.decimals)
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.Bold
            font.features: ({ "tnum": 1 })
        }
    }
    SizeSlider {
        width: parent.width
        height: 36
        from: 0
        to: 1
        value: ls.value
        onMoved: (v) => ls.moved(v)
    }
}
