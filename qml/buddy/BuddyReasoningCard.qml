import QtQuick

// The model's own reasoning, shown as a quiet flat card when show-thinking is on.
Rectangle {
    id: card

    property string reasoning: ""
    property bool shown: false

    visible: shown
    height: reasoningInner.implicitHeight + 14
    radius: 9
    color: Qt.rgba(1, 1, 1, 0.035)

    Column {
        id: reasoningInner
        x: 10
        y: 7
        width: card.width - 20
        spacing: 3

        Row {
            spacing: 5
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("THINKING")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontXs
                font.weight: Font.Bold
                font.letterSpacing: 0.5
            }
        }

        Text {
            width: parent.width
            text: card.reasoning.replace(/[#*`]/g, "").trim()
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.15
        }
    }
}
