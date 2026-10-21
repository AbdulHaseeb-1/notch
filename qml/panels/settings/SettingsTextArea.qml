pragma ComponentBehavior: Bound
import QtQuick

// A multi-line text box that matches SettingsFieldBox: a label, the box, a hint under it.
// The text scrolls inside the box when it is longer than the box.
Item {
    id: ta
    property string label: ""
    property string hint: ""
    property string placeholder: ""
    property string text: ""
    property real boxHeight: 120
    property bool mono: false
    signal edited(string text)

    width: parent ? parent.width : 260
    implicitHeight: (label.length > 0 ? 21 : 0) + boxHeight + (hint.length > 0 ? hintText.implicitHeight + 6 : 0)

    Text {
        id: taLabel
        visible: ta.label.length > 0
        anchors.top: parent.top
        width: parent.width
        height: 15
        text: ta.label
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: taBox
        anchors.top: taLabel.visible ? taLabel.bottom : parent.top
        anchors.topMargin: taLabel.visible ? 6 : 0
        width: parent.width
        height: ta.boxHeight
        radius: 10
        color: Theme.field
        border.width: 1
        border.color: taInput.activeFocus ? Theme.accent : Theme.fieldBorder
        Behavior on border.color { ColorAnimation { duration: 130 } }
        clip: true

        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 9
            contentWidth: width
            contentHeight: taInput.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            function ensureVisible(r) {
                if (contentY >= r.y)
                    contentY = r.y
                else if (contentY + height <= r.y + r.height)
                    contentY = r.y + r.height - height
            }

            TextEdit {
                id: taInput
                width: flick.width
                text: ta.text
                color: Theme.ink
                selectionColor: Theme.accent
                selectedTextColor: Theme.inverse
                font.family: ta.mono ? "Consolas" : Theme.font
                font.pixelSize: Theme.fontBase
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                onTextChanged: if (activeFocus && text !== ta.text) ta.edited(text)
                onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
                Accessible.role: Accessible.EditableText
                Accessible.name: ta.label.length > 0 ? ta.label : ta.placeholder

                Text {
                    visible: taInput.text.length === 0
                    text: ta.placeholder
                    color: Theme.textFaint
                    font: taInput.font
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            z: -1
            cursorShape: Qt.IBeamCursor
            onClicked: taInput.forceActiveFocus()
        }
    }

    Text {
        id: hintText
        anchors.top: taBox.bottom
        anchors.topMargin: 6
        visible: ta.hint.length > 0
        width: parent.width
        text: ta.hint
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
    }
}
