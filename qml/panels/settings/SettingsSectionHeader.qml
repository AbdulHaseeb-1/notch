pragma ComponentBehavior: Bound
import QtQuick

Item {
    property string title
    property string rightText: ""
    width: parent ? parent.width : 300
    height: 20

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        text: parent.title.toUpperCase()
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.Bold
        font.letterSpacing: 0.8
    }
    Text {
        visible: parent.rightText.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        text: parent.rightText
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.DemiBold
    }
}
