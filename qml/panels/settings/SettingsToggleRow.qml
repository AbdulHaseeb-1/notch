pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: tr
    property string label
    property string sub: ""
    property bool checked
    property bool enabled2: true
    signal toggled(bool checked)

    width: parent ? parent.width : 300
    implicitHeight: Math.max(36, textCol.implicitHeight + 6)

    Column {
        id: textCol
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        opacity: tr.enabled2 ? 1 : 0.45

        Text {
            text: tr.label
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontMd
            font.weight: Font.DemiBold
        }
        Text {
            visible: tr.sub.length > 0
            width: parent.width
            text: tr.sub
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            lineHeight: 1.15
        }
    }

    Toggle {
        id: sw
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        checked: tr.checked
        enabled2: tr.enabled2
        onToggled: (c) => tr.toggled(c)
    }
}
