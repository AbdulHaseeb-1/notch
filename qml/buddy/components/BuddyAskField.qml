pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: af
    property alias input: afInput
    property string hint: qsTr("Ask more…")
    signal sent(string text)
    signal escaped()
    radius: height / 2
    color: Qt.darker(Theme.field, 1.22)
    border.width: 0

    function send() {
        const q = afInput.text.trim()
        if (q.length > 0)
            af.sent(q)
    }

    TextInput {
        id: afInput
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: afSendBtn.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.ink
        selectionColor: Theme.accent
        selectedTextColor: Theme.inverse
        font.family: Theme.font
        font.pixelSize: Theme.fontBase
        font.weight: Font.Medium
        clip: true
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.name: af.hint
        Keys.onReturnPressed: (event) => { af.send(); event.accepted = true }
        Keys.onEnterPressed: (event) => { af.send(); event.accepted = true }
        Keys.onEscapePressed: (event) => { af.escaped(); event.accepted = true }

        Text {
            anchors.fill: parent
            visible: afInput.text.length === 0
            verticalAlignment: Text.AlignVCenter
            text: af.hint
            color: Theme.textFaint
            font: afInput.font
            elide: Text.ElideRight
        }
    }

    BuddySendButton {
        id: afSendBtn
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        active: afInput.text.trim().length > 0
        onClicked: af.send()
    }
}
