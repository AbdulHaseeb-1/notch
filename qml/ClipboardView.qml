import QtQuick

// Clipboard page of the hub: what you copied recently, one click to copy it
// again. Off until the user turns it on; nothing is saved to disk.
Item {
    id: view

    signal toastRequested(string text)

    Theme { id: t }

    readonly property int rowH: 58
    readonly property int maxRows: 5

    implicitWidth: 348
    implicitHeight: clipboard.enabled ? enabledCol.implicitHeight : offCol.implicitHeight

    function ago(when) {
        const sec = Math.max(0, Math.round((Date.now() - when.getTime()) / 1000))
        if (sec < 45)
            return qsTr("just now")
        const min = Math.round(sec / 60)
        if (min < 60)
            return qsTr("%1 min ago").arg(min)
        const hr = Math.round(min / 60)
        if (hr < 24)
            return qsTr("%1 h ago").arg(hr)
        return qsTr("%1 d ago").arg(Math.round(hr / 24))
    }

    // ---- off: ask first
    Column {
        id: offCol
        visible: !clipboard.enabled
        width: parent.width
        topPadding: 30
        bottomPadding: 28
        spacing: 12

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: 56
            radius: 28
            color: t.raised
            border.width: 1
            border.color: t.borderSoft
            Glyph {
                anchors.centerIn: parent
                name: "clipboard"
                size: 26
                color: t.ink
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Clipboard history")
            color: t.ink
            font.family: t.font
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 64
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.12
            text: qsTr("Keeps the text you copy so you can copy it again. It stays in memory only and is gone when Noch quits. Copies from password managers are skipped.")
            color: t.textDim
            font.family: t.font
            font.pixelSize: 12
        }
        Item { width: 1; height: 2 }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: onLabel.implicitWidth + 36
            height: 36
            radius: 18
            color: onHit.containsMouse ? t.inkSoft : t.ink
            scale: onHit.pressed ? 0.95 : 1
            Behavior on scale { SpringAnimation { spring: 6; damping: 0.35; epsilon: 0.0005 } }
            Behavior on color { ColorAnimation { duration: 120 } }
            Text {
                id: onLabel
                anchors.centerIn: parent
                text: qsTr("Turn on")
                color: t.inverse
                font.family: t.font
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            MouseArea {
                id: onHit
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: clipboard.enabled = true
            }
        }
    }

    // ---- on: the list
    Column {
        id: enabledCol
        visible: clipboard.enabled
        width: parent.width

        Item {
            width: parent.width
            height: 40

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: clipboard.count === 0 ? qsTr("Nothing yet")
                    : (clipboard.count === 1 ? qsTr("1 item") : qsTr("%1 items").arg(clipboard.count))
                color: t.textDim
                font.family: t.font
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Repeater {
                    model: [
                        { label: qsTr("Clear"), action: "clear" },
                        { label: qsTr("Turn off"), action: "off" }
                    ]
                    delegate: Item {
                        id: link
                        required property var modelData
                        width: linkText.implicitWidth + 16
                        height: 26
                        Text {
                            id: linkText
                            anchors.centerIn: parent
                            text: link.modelData.label
                            color: linkHit.containsMouse ? t.ink : t.textDim
                            font.family: t.font
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: linkHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (link.modelData.action === "clear")
                                    clipboard.clearUnpinned()
                                else
                                    clipboard.enabled = false
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: t.borderSoft }

        // Empty state.
        Item {
            visible: clipboard.count === 0
            width: parent.width
            height: 124
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Copy some text and it shows up here.")
                color: t.textDim
                font.family: t.font
                font.pixelSize: 13
            }
        }

        ListView {
            id: list
            visible: clipboard.count > 0
            width: parent.width
            height: Math.min(contentHeight, view.rowH * view.maxRows) + 8
            topMargin: 4
            bottomMargin: 4
            clip: true
            interactive: contentHeight > view.rowH * view.maxRows
            boundsBehavior: Flickable.StopAtBounds
            model: clipboard.items

            delegate: Item {
                id: row
                required property int index
                required property var modelData

                width: list.width
                height: view.rowH
                readonly property bool hot: rowHover.hovered

                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.right: actions.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.modelData.preview
                        color: t.text
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        lineHeight: 1.05
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: (row.modelData.lines > 1 ? qsTr("%1 lines").arg(row.modelData.lines)
                                                       : qsTr("%1 chars").arg(row.modelData.chars))
                              + "  ·  " + view.ago(row.modelData.when)
                        color: t.textFaint
                        elide: Text.ElideRight
                        font.family: t.font
                        font.pixelSize: 11
                    }
                }

                TapHandler {
                    id: rowTap
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        if (clipboard.copy(row.index))
                            view.toastRequested(qsTr("Copied"))
                    }
                }

                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    Item {
                        width: 28
                        height: 28
                        opacity: row.hot || row.modelData.pinned ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 110 } }
                        Glyph {
                            anchors.centerIn: parent
                            name: "pin"
                            size: 15
                            stroke: 2
                            color: row.modelData.pinned ? t.ink : (pinHit.containsMouse ? t.ink : t.textDim)
                            rotation: row.modelData.pinned ? 0 : 35
                            Behavior on rotation { SpringAnimation { spring: 5; damping: 0.3; epsilon: 0.05 } }
                        }
                        MouseArea {
                            id: pinHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // Hidden must mean unhittable: without this the
                            // transparent action spots swallow clicks.
                            enabled: parent.opacity > 0.5
                            onClicked: clipboard.togglePin(row.index)
                        }
                    }
                    Item {
                        width: 28
                        height: 28
                        opacity: row.hot ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 110 } }
                        Glyph {
                            anchors.centerIn: parent
                            name: "close"
                            size: 14
                            stroke: 2
                            color: delHit.containsMouse ? t.danger : t.textDim
                        }
                        MouseArea {
                            id: delHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: parent.opacity > 0.5
                            onClicked: clipboard.remove(row.index)
                        }
                    }
                }
            }
        }
    }
}
