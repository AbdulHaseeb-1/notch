import QtQuick

// Research digest card. Everything shown is a local sample note — BuddyRouter
// wrote it and Router says so on screen. The listed sources are real, famous
// front pages offered as *suggested reading*, never presented as fetched
// articles; nothing here claims to be live reporting.
Item {
    id: card

    property var host: null

    readonly property var digest: host ? host.payload : null

    // Only research payloads carry a sentiment block; other kinds (the help
    // card) share this file with an empty payload, so reads are guarded.
    readonly property bool ready: !!digest && digest.sentiment !== undefined

    Theme { id: t }

    function sentimentColor(s) {
        if (s >= 0.62) return t.success
        if (s <= 0.38) return t.danger
        return t.accent
    }

    // Tint helpers for chips drawn over glass.
    function tintSoft(c) { return Qt.rgba(c.r, c.g, c.b, 0.13) }

    function tintEdge(c) { return Qt.rgba(c.r, c.g, c.b, 0.5) }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: bodyCol.implicitHeight + 24
        interactive: contentHeight > height + 1
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: bodyCol
            x: 14
            y: 12
            width: card.width - 28
            spacing: 8

            Rectangle {
                width: parent.width
                height: bodyLabel.implicitHeight + 18
                radius: 10
                color: t.tile
                border.width: 1
                border.color: t.tileEdge

                Text {
                    id: bodyLabel
                    x: 10
                    y: 9
                    width: parent.width - 20
                    text: card.ready ? card.digest.summary : ""
                    color: t.text
                    font.family: t.font
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    lineHeight: 1.18
                }
            }

            Text {
                text: qsTr("Key findings")
                color: t.textDim
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Repeater {
                model: card.ready ? card.digest.findings : []

                Row {
                    width: bodyCol.width
                    spacing: 8
                    required property var modelData

                    Item {
                        width: 5
                        height: 16
                        Rectangle {
                            anchors.centerIn: parent
                            width: 5
                            height: 5
                            radius: 2.5
                            color: t.accent
                        }
                    }
                    Text {
                        width: parent.width - 13
                        text: parent.modelData
                        color: t.text
                        font.family: t.font
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        lineHeight: 1.18
                    }
                }
            }

            // Sentiment row (sample-tagged).
            Column {
                width: parent.width
                spacing: 6

                Row {
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Assessment")
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: sentLabel.implicitWidth + 16
                        height: 20
                        radius: 10
                        // Soft tint of the verdict colour:
                        color: card.ready ? card.tintSoft(card.sentimentColor(card.digest.sentiment.score))
                                          : "transparent"
                        border.width: 1
                        border.color: card.ready ? card.tintEdge(card.sentimentColor(card.digest.sentiment.score))
                                                 : t.border

                        Text {
                            id: sentLabel
                            anchors.centerIn: parent
                            text: card.ready ? card.digest.sentiment.label : ""
                            color: card.ready ? card.sentimentColor(card.digest.sentiment.score) : t.textDim
                            font.family: t.font
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: card.ready ? (Math.round(card.digest.sentiment.score * 100) + "%")
                                        : ""
                        color: t.textFaint
                        font.family: t.font
                        font.pixelSize: 10
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 5
                    radius: 2.5
                    color: t.track
                    Rectangle {
                        width: card.ready ? Math.max(6, parent.width * card.digest.sentiment.score) : 6
                        height: parent.height
                        radius: 2.5
                        color: card.ready ? card.sentimentColor(card.digest.sentiment.score) : t.track
                        Behavior on width { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
                    }
                }
            }

            Text {
                text: qsTr("Suggested reading (not fetched — just famous desks)")
                color: t.textDim
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Flow {
                width: parent.width
                spacing: 8

                Repeater {
                    model: card.ready ? card.digest.sources : []

                    delegate: Rectangle {
                        id: srcBtn
                        required property var modelData
                        width: srcTxt.implicitWidth + 46
                        height: 24
                        radius: 12
                        color: srcHit.containsMouse || srcHit.pressed ? t.tileActive : t.tile
                        border.width: 1
                        border.color: t.border

                        Row {
                            anchors.centerIn: parent
                            spacing: 5

                            Glyph {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "globe"
                                size: 11
                                stroke: 2
                                color: srcHit.containsMouse ? t.ink : t.textDim
                            }
                            Text {
                                id: srcTxt
                                anchors.verticalCenter: parent.verticalCenter
                                text: srcBtn.modelData.name
                                color: srcHit.containsMouse ? t.ink : t.text
                                font.family: t.font
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            Glyph {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "open"
                                size: 10
                                stroke: 2
                                color: srcHit.containsMouse ? t.ink : t.textFaint
                            }
                        }
                        MouseArea {
                            id: srcHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(srcBtn.modelData.url)
                        }
                    }
                }
            }

            Text {
                text: qsTr("Sample content — Noch has no live feed wired up, so nothing here is real-time.")
                color: t.textFaint
                font.family: t.font
                font.pixelSize: 10
                width: parent.width
                wrapMode: Text.WordWrap
            }
        }
    }
}
