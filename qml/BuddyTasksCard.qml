import QtQuick

// The productivity card: a checkable list of plan items with progress, add, and
// clear-done. Changes emit `planEdited` — Buddy persists them through the app's
// Settings storage, so "saved for tomorrow" is a true statement. If the user
// changes nothing, nothing is saved and nothing is claimed saved.
Item {
    id: card

    property var host: null              // BuddyPanel (signals out)
    property var items: []               // [{text, done}]
    property string day: ""              // ISO date the plan belongs to


    function syncFromHost() {
        if (!host || !host.payload || !host.payload.items)
            return
        items = host.payload.items.slice()
        const day = host.payload.day || new Date().toISOString().slice(0, 10)
        if (day !== card.day) {
            card.day = day
        }
    }
    onHostChanged: syncFromHost()
    Component.onCompleted: syncFromHost()

    Connections {
        target: card.host
        function onPayloadChanged() { card.syncFromHost() }
    }

    function emitPlan() {
        if (host)
            host.planEdited({ day: card.day, items: items })
    }

    function toggleDone(i) {
        if (i < 0 || i >= items.length)
            return
        const next = items.slice()
        next[i] = { text: next[i].text, done: !next[i].done }
        items = next
        emitPlan()
    }

    function removeAt(i) {
        if (i < 0 || i >= items.length)
            return
        const next = items.slice()
        next.splice(i, 1)
        items = next
        emitPlan()
    }

    function addItem(text) {
        const trimmed = String(text || "").trim()
        if (trimmed.length === 0)
            return
        const next = items.slice()
        next.push({ text: trimmed, done: false })
        items = next
        addField.text = ""
        emitPlan()
        list.positionViewAtEnd()
    }

    function clearDone() {
        const next = items.filter(function (it) { return !it.done })
        if (next.length === items.length)
            return
        items = next
        emitPlan()
    }

    readonly property int doneCount: items.filter(function (it) { return it.done }).length
    readonly property real frac: items.length ? doneCount / items.length : 0

    Column {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        // progress row
        Item {
            width: parent.width
            height: 22

            Text {
                id: dayLabel
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: card.day
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: 10
            }

            Rectangle {
                anchors.right: doneLabel.left
                anchors.rightMargin: 10
                anchors.left: dayLabel.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                height: 5
                radius: 2.5
                color: Theme.track

                readonly property real frac: card.frac

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(3, parent.width * card.frac)
                    height: 5
                    radius: 2.5
                    color: card.frac >= 1 ? Theme.success : Theme.accent
                    Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                }
            }

            Text {
                id: doneLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: card.doneCount + " / " + card.items.length + qsTr(" done")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }

        // task list
        ListView {
            id: list
            width: parent.width
            height: parent.height - 68
            spacing: 2
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            model: card.items

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: list.width
                height: 30

                HoverHandler { id: hot }

                Rectangle {
                    id: checkPin
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 9
                    color: row.modelData.done ? Theme.accent
                          : (checkHit.containsMouse || checkHit.pressed ? Theme.tileActive : Theme.tile)
                    border.width: row.modelData.done ? 0 : 1.5
                    border.color: checkHit.containsMouse ? Theme.textDim : Theme.borderStrong
                    scale: checkHit.pressed ? 0.88 : 1

                    Glyph {
                        anchors.centerIn: parent
                        visible: row.modelData.done
                        name: "check"
                        size: 10
                        stroke: 2.4
                        color: Theme.surface
                    }
                    MouseArea {
                        id: checkHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: card.toggleDone(row.index)
                    }
                }

                Text {
                    anchors.left: checkPin.right
                    anchors.leftMargin: 9
                    anchors.right: delPin.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.text
                    color: row.modelData.done ? Theme.textFaint : Theme.text
                    font.family: Theme.font
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    opacity: row.modelData.done ? 0.62 : 1
                }

                // delete (only on hover; hidden means unhittable)
                Item {
                    id: delPin
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    opacity: hot.hovered ? 1 : 0

                    Glyph {
                        anchors.centerIn: parent
                        name: "close"
                        size: 11
                        stroke: 2
                        color: delHit.containsMouse ? Theme.danger : Theme.textFaint
                    }
                    MouseArea {
                        id: delHit
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: parent.opacity > 0.5
                        cursorShape: Qt.PointingHandCursor
                        onClicked: card.removeAt(row.index)
                    }
                    Behavior on opacity { NumberAnimation { duration: 110 } }
                }
            }
        }   // ListView

        // done housekeeping + composer
        Item {
            width: parent.width
            height: 30

            Glyph {
                anchors.left: parent.left
                anchors.leftMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                name: "plus"
                size: 13
                stroke: 1.8
                color: addField.text.length === 0 ? Theme.textFaint : Theme.accent
            }

            TextInput {
                id: addField
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.right: clearDone.visible ? clearDone.left : parent.right
                anchors.rightMargin: clearDone.visible ? 8 : 4
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: 12
                clip: true
                selectByMouse: true

                Text {
                    anchors.fill: parent
                    visible: parent.text.length === 0
                    text: qsTr("Add a task for today…")
                    color: Theme.textFaint
                    font: parent.font
                    verticalAlignment: TextInput.AlignVCenter
                }

                Keys.onReturnPressed: (event) => { card.addItem(text); event.accepted = true }
                Keys.onEnterPressed: (event) => { card.addItem(text); event.accepted = true }
            }

            Text {
                id: clearDone
                visible: card.doneCount > 0
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear done")
                color: clearDoneHit.containsMouse ? Theme.ink : Theme.accent
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold

                MouseArea {
                    id: clearDoneHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.clearDone()
                }
            }
        }
    }
}
