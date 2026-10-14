import QtQuick

// Hover preview for a program with windows open: a snapshot of each window
// with its title underneath. Clicking a window brings it to the front.
Item {
    id: preview

    Theme { id: t }

    property var windows: []
    property real stamp: 0
    readonly property bool hovered: hover.hovered

    readonly property real thumbWidth: 180
    readonly property real thumbHeight: 101
    readonly property real padX: 12
    readonly property real padY: 12

    signal activated(real windowId)

    implicitWidth: row.width + 2 * padX
    implicitHeight: row.height + 2 * padY

    Glass {
        anchors.fill: parent
        radius: 22
    }

    HoverHandler { id: hover }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10

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
                    color: "#0b0c0f"
                    clip: true
                    border.width: 1
                    border.color: thumb.hot ? t.white : "#2b2f33"

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
                    anchors.topMargin: 6
                    width: parent.width
                    text: thumb.modelData.title
                    elide: Text.ElideRight
                    color: thumb.hot ? t.white : t.textDim
                    font.family: t.font
                    font.pixelSize: 12
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
