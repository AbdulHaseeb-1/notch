pragma ComponentBehavior: Bound
import QtQuick

// Clipboard page of the hub: what you copied recently, one click to copy it
// again. Off until the user turns it on; nothing is saved to disk.
Item {
    id: view

    signal toastRequested(string text)


    readonly property int rowH: 58
    readonly property int maxRows: 5

    implicitWidth: 348
    implicitHeight: Clipboard.enabled ? enabledCol.implicitHeight : offCol.implicitHeight

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
        visible: !Clipboard.enabled
        width: parent.width
        topPadding: 30
        bottomPadding: 28
        spacing: 12

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: 56
            radius: 28
            color: Theme.raised
            border.width: 1
            border.color: Theme.borderSoft
            Glyph {
                anchors.centerIn: parent
                name: "clipboard"
                size: 26
                color: Theme.ink
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Clipboard history")
            color: Theme.ink
            font.family: Theme.font
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
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: 12
        }
        Item { width: 1; height: 2 }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: onLabel.implicitWidth + 36
            height: 36
            radius: 18
            color: onHit.containsMouse ? Theme.inkSoft : Theme.ink
            scale: onHit.pressed ? 0.95 : 1
            Behavior on scale { SpringAnimation { spring: 6; damping: 0.35; epsilon: 0.0005 } }
            Behavior on color { ColorAnimation { duration: 120 } }
            Text {
                id: onLabel
                anchors.centerIn: parent
                text: qsTr("Turn on")
                color: Theme.inverse
                font.family: Theme.font
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            MouseArea {
                id: onHit
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Clipboard.enabled = true
            }
        }
    }

    // ---- on: the list
    Column {
        id: enabledCol
        visible: Clipboard.enabled
        width: parent.width

        Item {
            width: parent.width
            height: 40

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: Clipboard.count === 0 ? qsTr("Nothing yet")
                    : (Clipboard.count === 1 ? qsTr("1 item") : qsTr("%1 items").arg(Clipboard.count))
                color: Theme.textDim
                font.family: Theme.font
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
                            color: linkHit.containsMouse ? Theme.ink : Theme.textDim
                            font.family: Theme.font
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
                                    Clipboard.clearUnpinned()
                                else
                                    Clipboard.enabled = false
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }

        // Empty state.
        Item {
            visible: Clipboard.count === 0
            width: parent.width
            height: 124
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Copy some text and it shows up here.")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: 13
            }
        }

        ListView {
            id: list
            visible: Clipboard.count > 0
            width: parent.width
            height: Math.min(contentHeight, view.rowH * view.maxRows) + 8
            topMargin: 4
            bottomMargin: 4
            clip: true
            interactive: contentHeight > view.rowH * view.maxRows
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 320
            model: Clipboard.items

            delegate: Item {
                id: row
                required property int index
                required property var modelData

                width: list.width
                // Size to the content so one- and two-line clips get the
                // same padding (fixed rows left uneven gaps).
                height: Math.max(view.rowH - 10, rowText.implicitHeight + 20)
                readonly property bool hot: rowHover.hovered

                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }

                // What kind of text this is, for the tile glyph.
                readonly property string kind: {
                    const t = String(row.modelData.preview || "").trim()
                    if (/^(https?:\/\/|www\.)\S+$/i.test(t)) return "globe"
                    if (/^(\$ |>|git |npm |cmake |cd |ls |dir |python|pip |winget )/.test(t)
                            || /[{};]\s*$/.test(t)) return "terminal"
                    if (row.modelData.lines > 1) return "doc"
                    return "text"
                }

                // Glass row: transparent at rest, lifted on hover.
                Rectangle {
                    x: 10
                    y: 2
                    width: parent.width - 20
                    height: parent.height - 4
                    radius: Theme.radiusMd
                    color: row.hot ? Theme.glassFillHover : "transparent"
                    border.width: row.hot ? 1 : 0
                    border.color: Theme.glassBorder
                }

                Rectangle {
                    id: kindTile
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    radius: Theme.radiusSm + 1
                    color: Theme.glassFillActive
                    border.width: 1
                    border.color: Theme.glassBorder
                    Glyph {
                        anchors.centerIn: parent
                        name: row.kind
                        size: 14
                        color: row.hot ? Theme.ink : Theme.inkSoft
                    }
                }

                Column {
                    id: rowText
                    anchors.left: kindTile.right
                    anchors.leftMargin: 11
                    anchors.right: actions.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.modelData.preview
                        color: Theme.text
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        lineHeight: 1.05
                        font.family: Theme.font
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: (row.modelData.lines > 1 ? qsTr("%1 lines").arg(row.modelData.lines)
                                                       : qsTr("%1 chars").arg(row.modelData.chars))
                              + "  ·  " + view.ago(row.modelData.when)
                        color: Theme.textFaint
                        elide: Text.ElideRight
                        font.family: Theme.font
                        font.pixelSize: 11
                    }
                }

                TapHandler {
                    id: rowTap
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        if (Clipboard.copy(row.index))
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
                            color: row.modelData.pinned ? Theme.ink : (pinHit.containsMouse ? Theme.ink : Theme.textDim)
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
                            onClicked: Clipboard.togglePin(row.index)
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
                            color: delHit.containsMouse ? Theme.danger : Theme.textDim
                        }
                        MouseArea {
                            id: delHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: parent.opacity > 0.5
                            onClicked: Clipboard.remove(row.index)
                        }
                    }
                }
            }
        }
    }
}
