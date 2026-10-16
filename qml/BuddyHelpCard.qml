import QtQuick

// The "what can you do" card. Everything on it is a real Noch action wired to
// the touch points the palette already provides: the command palette itself,
// the focus timer, clipboard history, mic mute and settings. It is also the
// honest fallback when a request does not map onto anything the app can do.
Item {
    id: card

    property var host: null   // BuddyPanel → capability(id)/signals out

    Theme { id: t }

    readonly property var rows: [
        { id: "palette", icon: "search",
          text: qsTr("Open the command palette"),
          sub: qsTr("Search apps, windows, calculate and run the web search.") },
        { id: "timer", icon: "timer",
          text: qsTr("Start a 25-minute focus timer"),
          sub: qsTr("Real focus session with the live timer pill.") },
        { id: "clipboard", icon: "clipboard",
          text: qsTr("Open clipboard history"),
          sub: qsTr("Pin or restore anything copied recently.") },
        { id: "mic", icon: "mic",
          text: qsTr("Toggle microphone mute"),
          sub: qsTr("Same as the notch's mic pill.") },
        { id: "settings", icon: "settings",
          text: qsTr("Open Noch settings"),
          sub: qsTr("Themes, size, hotkeys and more.") }
    ]

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: helpCol.implicitHeight + 24
        interactive: contentHeight > height + 1
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: helpCol
            x: 12
            y: 12
            width: card.width - 24
            spacing: 6

            Text {
                width: parent.width - 4
                x: 2
                text: qsTr("I'm Noch's little helper — no online model, no secret sauce. Here's what this desktop can really do:")
                color: t.textDim
                font.family: t.font
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }

            Repeater {
                model: card.rows

                delegate: Item {
                    id: row
                    required property var modelData
                    width: helpCol.width
                    height: 44

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: rowHit.containsMouse ? t.tile : "transparent"
                        border.width: rowHit.containsMouse ? 1 : 0
                        border.color: t.tileEdge
                    }

                    Item {
                        id: iconPin
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        Rectangle {
                            anchors.fill: parent
                            radius: 13
                            color: t.tileActive
                        }
                        Glyph {
                            anchors.centerIn: parent
                            name: row.modelData.icon
                            size: 14
                            stroke: 2
                            color: t.ink
                        }
                    }

                    Column {
                        anchors.left: iconPin.right
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            text: row.modelData.text
                            color: t.text
                            font.family: t.font
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: row.modelData.sub
                            color: t.textFaint
                            font.family: t.font
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: rowHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (card.host)
                                       card.host.capability(row.modelData.id)
                    }
                }
            }

            Text {
                width: parent.width - 4
                x: 2
                text: qsTr("Try: “Analyze BTC/USDT”, “Summarize gold news”, “Plan my day”, or “Open Spotify”.")
                color: t.textFaint
                font.family: t.font
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }
}
