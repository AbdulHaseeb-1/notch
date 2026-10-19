pragma ComponentBehavior: Bound
import QtQuick

// Hover preview for a program with windows open. A header names the program
// and counts its windows; below, a snapshot of each window (the first three)
// with its title. Clicking a window brings it to the front.
Item {
    id: preview


    property string programName: ""
    property int windowCount: 0
    property var windows: []          // [{windowId, title}], already capped
    property real stamp: 0            // changes each time the preview opens
    property real anchorX: 0          // centre of the hovered tile
    property real containerWidth: 0   // width of the notch window
    property real cardTop: 0             // y of the card

    readonly property bool hovered: hover.hovered
    readonly property real pad: 14
    readonly property real gap: 10
    readonly property real thumbWidth: 172
    readonly property real thumbHeight: Math.round(thumbWidth * 10 / 16)
    readonly property real contentWidth: Math.max(200, grid.width)

    signal activated(real windowId)

    x: Math.round(Math.max(12, Math.min(containerWidth - width - 12, anchorX - width / 2)))
    y: cardTop
    width: contentWidth + 2 * pad
    height: pad + header.height + 10 + grid.height + pad

    Glass {
        anchors.fill: parent
        radius: 20
    }

    HoverHandler { id: hover }

    // Header: the program name on the left, the window count on the right.
    Item {
        id: header
        x: preview.pad
        y: preview.pad
        width: preview.contentWidth
        height: 18

        Text {
            id: nameText
            anchors.left: parent.left
            anchors.right: countText.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: preview.programName
            color: Theme.ink
            elide: Text.ElideRight
            font.family: Theme.font
            font.pixelSize: Theme.fontMd
            font.weight: Font.Bold
        }

        Text {
            id: countText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: preview.windowCount === 1 ? qsTr("1 window") : qsTr("%1 windows").arg(preview.windowCount)
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
    }

    // The snapshots, centred under the header.
    Row {
        id: grid
        x: preview.pad + Math.round((preview.contentWidth - width) / 2)
        y: header.y + header.height + 10
        spacing: preview.gap

        Repeater {
            model: preview.windows

            delegate: Item {
                id: thumb

                required property var modelData
                readonly property bool hot: hit.containsMouse

                width: preview.thumbWidth
                height: preview.thumbHeight + 28

                Rectangle {
                    id: frame
                    width: parent.width
                    height: preview.thumbHeight
                    radius: 12
                    color: Theme.frame
                    clip: true
                    border.width: 1
                    border.color: thumb.hot ? Theme.accent : Theme.border
                    Behavior on border.color { ColorAnimation { duration: 140 } }

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: "image://preview/" + thumb.modelData.windowId + "/" + preview.stamp
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        smooth: true
                        mipmap: true
                    }
                }

                Text {
                    anchors.top: frame.bottom
                    anchors.topMargin: 7
                    width: parent.width
                    text: thumb.modelData.title
                    elide: Text.ElideRight
                    color: thumb.hot ? Theme.ink : Theme.text
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    id: hit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: preview.activated(thumb.modelData.windowId)
                }
            }
        }
    }
}
