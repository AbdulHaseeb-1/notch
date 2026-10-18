import QtQuick

// Context menu for a pinned app (open/rename/show in folder/move/unpin).
Flyout {
    id: menu

    property var root: null

    open: root.menuIndex >= 0
    radius: 18
    baseY: root.shownY + root.pillH + 10
    property bool renaming: false
    property var shownApp: null
    readonly property var app: shownApp

    onOpenChanged: {
        if (open) {
            shownApp = root.menuIndex >= 0 ? root.apps[root.menuIndex] : null
            renaming = false
        }
    }
    Connections {
        target: root
        function onMenuIndexChanged() {
            if (root.menuIndex >= 0) {
                menu.shownApp = root.apps[root.menuIndex]
                menu.renaming = false
            }
        }
    }

    width: 280
    height: menuCol.implicitHeight + 12
    x: Math.round(Math.max(12, Math.min(root.width - width - 12, root.menuX - width / 2)))

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
                    color: t.ink
                    elide: Text.ElideRight
                    font.family: t.font
                    font.pixelSize: 15
                    font.weight: Font.ExtraBold
                }
                Rectangle {
                    visible: menu.renaming
                    width: parent.width
                    height: 26
                    radius: 6
                    color: t.raised
                    border.width: 1
                    border.color: t.accent
                    TextInput {
                        id: renameInput
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: TextInput.AlignVCenter
                        color: t.ink
                        selectionColor: t.ink
                        selectedTextColor: t.inverse
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        clip: true
                        onAccepted: {
                            const i = root.menuRowIndex()
                            if (i >= 0)
                                dock.rename(i, text)
                            menu.renaming = false
                        }
                        Keys.onEscapePressed: menu.renaming = false
                    }
                }
                Text {
                    width: parent.width
                    text: menu.app ? (root.running.indexOf(menu.app.exe) >= 0 ? qsTr("Running") : (menu.app.path || "")) : ""
                    color: t.textDim
                    elide: Text.ElideMiddle
                    font.family: t.font
                    font.pixelSize: 11
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: t.borderSoft }
        Item { width: 1; height: 4 }

        MenuRow {
            width: parent.width
            label: qsTr("Open")
            icon: "open"
            hint: "↵"
            onTriggered: {
                const i = root.menuRowIndex()
                if (i >= 0)
                    root.launchApp(i)
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
                const i = root.menuRowIndex()
                if (i >= 0)
                    dock.revealInFolder(i)
                root.menuIndex = -1
            }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Move left")
            icon: "left"
            readonly property int liveIndex: root.menuRowIndex()
            visible: liveIndex > 0
            onTriggered: { dock.move(liveIndex, liveIndex - 1); root.menuIndex = -1 }
        }
        MenuRow {
            width: parent.width
            label: qsTr("Move right")
            icon: "right"
            readonly property int liveIndex: root.menuRowIndex()
            visible: liveIndex >= 0 && liveIndex < root.apps.length - 1
            onTriggered: { dock.move(liveIndex, liveIndex + 1); root.menuIndex = -1 }
        }
        Item { width: 1; height: 4 }
        Rectangle { width: parent.width; height: 1; color: t.borderSoft }
        Item { width: 1; height: 4 }
        MenuRow {
            width: parent.width
            label: qsTr("Unpin")
            icon: "unpin"
            danger: true
            onTriggered: {
                const i = root.menuRowIndex()
                root.menuIndex = -1
                if (i >= 0)
                    dock.unpin(i)
            }
        }
    }

    Theme { id: t }
}