import QtQuick

// The agent's TodoWrite checklist: each item shows as done, in progress or waiting.
Rectangle {
    id: card

    property var todos: []
    property bool shown: false

    visible: shown
    height: todoCol.implicitHeight + 14
    radius: 9
    color: Theme.tile
    border.width: 0
    border.color: Theme.borderSoft

    Column {
        id: todoCol
        x: 10
        y: 7
        width: card.width - 20
        spacing: 4

        Row {
            spacing: 5
            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                name: "tasks"
                size: 11
                stroke: 1.8
                color: Theme.accent
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("AGENT TASKS (%1)").arg(card.todos.length)
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontXs
                font.weight: Font.Bold
                font.letterSpacing: 0.5
            }
        }

        Repeater {
            model: card.todos
            Row {
                id: todoRow
                required property var modelData
                width: todoCol.width
                spacing: 6
                readonly property bool done: modelData.status === "completed"
                readonly property bool active: modelData.status === "in_progress"

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: 13
                    radius: 4
                    color: todoRow.done ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.18)
                         : (todoRow.active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent")
                    border.width: 1
                    border.color: todoRow.done ? Theme.success : (todoRow.active ? Theme.accent : Theme.borderStrong)

                    Glyph {
                        visible: todoRow.done || todoRow.active
                        anchors.centerIn: parent
                        name: todoRow.done ? "check" : "bolt"
                        size: 8.5
                        stroke: 2.0
                        color: todoRow.done ? Theme.success : Theme.accent
                    }
                }

                Text {
                    width: parent.width - 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(todoRow.modelData.content || "")
                    color: parent.done ? Theme.textDim : Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.strikeout: parent.done
                    elide: Text.ElideRight
                }
            }
        }
    }
}
