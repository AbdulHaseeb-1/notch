pragma ComponentBehavior: Bound
import QtQuick

// The heading of a section. `description` is a line under it for what the section does.
Item {
    id: sh
    property string title
    property string rightText: ""
    property string description: ""
    // Air above, so sections breathe apart.
    property real gap: 14
    width: parent ? parent.width : 300
    height: gap + 20 + (description.length > 0 ? descText.implicitHeight + 3 : 0)

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.leftMargin: 2
        y: sh.gap
        height: 20
        verticalAlignment: Text.AlignVCenter
        text: sh.title.toUpperCase()
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.Bold
        font.letterSpacing: 0.8
    }
    Text {
        visible: sh.rightText.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: sh.gap
        height: 20
        verticalAlignment: Text.AlignVCenter
        text: sh.rightText
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.DemiBold
    }
    Text {
        id: descText
        visible: sh.description.length > 0
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.right: parent.right
        y: sh.gap + 22
        text: sh.description
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
    }
}
