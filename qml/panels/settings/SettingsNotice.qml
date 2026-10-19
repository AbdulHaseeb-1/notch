pragma ComponentBehavior: Bound
import QtQuick

// A short note on a page: why part of it is empty, or a warning, with an optional action.
Rectangle {
    id: note
    property string text: ""
    property string icon: "info"
    property string action: ""
    property bool warning: false
    signal activated()

    width: parent ? parent.width : 300
    implicitHeight: Math.max(48, row.implicitHeight + 24)
    radius: Theme.radiusLg
    color: warning ? Theme.dangerSoft : Theme.glassFill
    border.width: 1
    border.color: warning ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35) : Theme.glassBorder

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: 14
        width: parent.width - 28
        spacing: 12
        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            name: note.icon
            size: 16
            color: note.warning ? Theme.danger : Theme.textDim
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 28 - (actionBtn.visible ? actionBtn.width + 12 : 0)
            text: note.text
            color: Theme.inkSoft
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
            wrapMode: Text.WordWrap
            lineHeight: 1.12
        }
        SettingsPillButton {
            id: actionBtn
            visible: note.action.length > 0
            anchors.verticalCenter: parent.verticalCenter
            label: note.action
            onClicked: note.activated()
        }
    }
}
