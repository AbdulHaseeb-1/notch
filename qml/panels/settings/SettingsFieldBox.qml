pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: fb
    property string label: ""
    property string hint: ""
    property string placeholder: ""
    property string text: ""
    property bool secret: false
    property bool mono: false
    property bool clearable: true
    property bool revealed: false
    signal edited(string text)
    signal accepted()

    readonly property bool hasText: text.length > 0

    width: parent ? parent.width : 260
    implicitHeight: (label.length > 0 ? 21 : 0) + 36 + (hint.length > 0 ? 19 : 0)

    Text {
        id: fbLabel
        visible: fb.label.length > 0
        anchors.top: parent.top
        width: parent.width
        height: 15
        text: fb.label
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: fbBox
        anchors.top: fbLabel.visible ? fbLabel.bottom : parent.top
        anchors.topMargin: fbLabel.visible ? 6 : 0
        width: parent.width
        height: 36
        radius: 10
        color: Theme.field
        border.width: 1
        border.color: fbInput.activeFocus ? Theme.accent : Theme.fieldBorder
        Behavior on border.color { ColorAnimation { duration: 130 } }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            onClicked: fbInput.forceActiveFocus()
        }
    }

    Row {
        id: fbTail
        anchors.right: parent.right
        anchors.rightMargin: 5
        anchors.verticalCenter: fbBox.verticalCenter
        spacing: 1

        Item {
            id: fbClear
            visible: fb.clearable && fb.hasText
            width: 22
            height: 22
            Glyph {
                anchors.centerIn: parent
                name: "close"
                size: 10
                stroke: 2.2
                color: fbClearHit.containsMouse ? Theme.text : Theme.textFaint
            }
            MouseArea {
                id: fbClearHit
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Clear")
                onClicked: {
                    fbInput.text = ""
                    fb.edited("")
                    fbInput.forceActiveFocus()
                }
            }
        }

        Item {
            visible: fb.secret
            width: 22
            height: 22
            Glyph {
                anchors.centerIn: parent
                name: fb.revealed ? "eye-off" : "eye"
                size: 13
                stroke: 1.7
                color: fbRevealHit.containsMouse ? Theme.text : Theme.textFaint
            }
            MouseArea {
                id: fbRevealHit
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: fb.revealed ? qsTr("Hide value") : qsTr("Show value")
                onClicked: fb.revealed = !fb.revealed
            }
        }
    }

    TextInput {
        id: fbInput
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.right: fbTail.left
        anchors.rightMargin: 5
        anchors.verticalCenter: fbBox.verticalCenter
        height: 20
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.ink
        selectionColor: Theme.accent
        selectedTextColor: Theme.inverse
        font.family: fb.mono ? "Consolas" : Theme.font
        font.pixelSize: Theme.fontMd
        clip: true
        selectByMouse: true
        echoMode: fb.secret && !fb.revealed ? TextInput.Password : TextInput.Normal
        text: fb.text
        onTextEdited: fb.edited(text)
        Keys.onReturnPressed: (event) => { fb.accepted(); event.accepted = true }
        Keys.onEnterPressed: (event) => { fb.accepted(); event.accepted = true }

        Accessible.role: Accessible.EditableText
        Accessible.name: fb.label.length > 0 ? fb.label : fb.placeholder

        Text {
            anchors.fill: parent
            visible: fbInput.text.length === 0
            verticalAlignment: TextInput.AlignVCenter
            text: fb.placeholder
            color: Theme.textFaint
            font: fbInput.font
            elide: Text.ElideRight
        }
    }

    Text {
        anchors.top: fbBox.bottom
        anchors.topMargin: 6
        visible: fb.hint.length > 0
        width: parent.width
        text: fb.hint
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        elide: Text.ElideMiddle
    }
}
