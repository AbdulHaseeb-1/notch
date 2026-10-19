import QtQuick

// Context menu for a pinned app (open/rename/show in folder/move/unpin).
Flyout {
    id: menu

    property var root: null

    open: menu.root.menuIndex >= 0
    radius: 18
    baseY: menu.root.shownY + menu.root.pillH + 10
    property bool renaming: false
    property var shownApp: null
    readonly property var app: shownApp

    onOpenChanged: {
        if (open) {
            shownApp = menu.root.menuIndex >= 0 ? menu.root.apps[menu.root.menuIndex] : null
            renaming = false
        }
    }
    Connections {
        target: menu.root
        function onMenuIndexChanged() {
            if (menu.root.menuIndex >= 0) {
                menu.shownApp = menu.root.apps[menu.root.menuIndex]
                menu.renaming = false
            }
        }
    }

    width: 280
    height: menuCol.implicitHeight + 12
    x: Math.round(Math.max(12, Math.min(menu.root.width - width - 12, menu.root.menuX - width / 2)))

    Column {
        id: menuCol
        y: 6
        width: parent.width

        Item {
            width: parent.width
            height: 52
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    visible: !menu.renaming
                    width: parent.width
                    text: menu.app ? menu.app.name : ""
                    color: Theme.ink
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: 15
                    font.weight: Font.ExtraBold
                }
                Rectangle {
                    visible: menu.renaming
                    width: parent.width
                    height: 26
                    radius: 6
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.accent
                    TextInput {
                        id: renameInput
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.ink
                        selectionColor: Theme.ink
                        selectedTextColor: Theme.inverse
                        font.family: Theme.font
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        clip: true
                        onAccepted: {
                            const i = menu.root.menuRowIndex()
                            if (i >= 0)
                                Dock.rename(i, text)
                            menu.renaming = false
                        }
                        Keys.onEscapePressed: menu.renaming = false
                    }
                }
                Text {
                    width: parent.width
                    text: menu.app ? (menu.root.running.indexOf(menu.app.exe) >= 0 ? qsTr("Running") : (menu.app.path || "")) : ""
                    color: Theme.textDim
                    elide: Text.ElideMiddle
                    font.family: Theme.font
                    font.pixelSize: 11
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }
        Item { width: 1; height: 4 }

        MenuRow {
            width: parent.width
            label: qsTr("Open")
            icon: "open"
            hint: "↵"
            onTriggered: {
                const i = menu.root.menuRowIndex()
                if (i >= 0)
                    menu.root.launchApp(i)
            }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Rename")
            icon: "edit"
            onTriggered: {
                renameInput.text = menu.app ? menu.app.name : ""
                menu.renaming = true
                renameInput.forceActiveFocus()
                renameInput.selectAll()
            }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Show in folder")
            icon: "folder"
            visible: !!(menu.app && menu.app.path)
            onTriggered: {
                const i = menu.root.menuRowIndex()
                if (i >= 0)
                    Dock.revealInFolder(i)
                menu.root.menuIndex = -1
            }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Move left")
            icon: "left"
            readonly property int liveIndex: menu.root.menuRowIndex()
            visible: liveIndex > 0
            onTriggered: { Dock.move(liveIndex, liveIndex - 1); menu.root.menuIndex = -1 }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Move right")
            icon: "right"
            readonly property int liveIndex: menu.root.menuRowIndex()
            visible: liveIndex >= 0 && liveIndex < menu.root.apps.length - 1
            onTriggered: { Dock.move(liveIndex, liveIndex + 1); menu.root.menuIndex = -1 }
        }
        Item { width: 1; height: 4 }
        Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }
        Item { width: 1; height: 4 }
        MenuRow {
            width: parent.width
            label: qsTr("Unpin")
            icon: "unpin"
            danger: true
            onTriggered: {
                const i = menu.root.menuRowIndex()
                menu.root.menuIndex = -1
                if (i >= 0)
                    Dock.unpin(i)
            }
        }
    }

}