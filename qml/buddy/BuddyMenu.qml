pragma ComponentBehavior: Bound
import QtQuick

// The menu that opens when Buddy is right-clicked: ask, talk, settings. It is a plain
// list of rows ({ id, label, icon, hint, danger }) in smoked glass; Buddy.qml decides what
// is in it and what each row does, and closes it when focus leaves Noch.
Item {
    id: menu

    property bool open: false
    property var rows: []
    signal picked(string id)

    width: 232
    height: list.height + 12
    visible: opacity > 0.01
    opacity: open ? 1 : 0
    scale: open ? 1 : 0.94
    transformOrigin: Item.Top
    Behavior on opacity { NumberAnimation { duration: 120 } }
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    Glass {
        anchors.fill: parent
        radius: 18
    }

    // Takes the clicks that miss a row, so they do not fall through to the orb underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    Column {
        id: list
        x: 6
        y: 6
        width: parent.width - 12

        Repeater {
            model: menu.rows
            delegate: MenuRow {
                id: row
                required property var modelData
                width: list.width
                implicitHeight: 38
                label: row.modelData.label
                icon: row.modelData.icon || ""
                hint: row.modelData.hint || ""
                danger: !!row.modelData.danger
                onTriggered: menu.picked(row.modelData.id)
            }
        }
    }
}
