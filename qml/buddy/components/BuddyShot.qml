pragma ComponentBehavior: Bound
import QtQuick

// A screenshot Buddy put into the chat (the share_screenshot tool). It eases in
// like the words around it, shows a soft placeholder until the picture has
// loaded, and opens full size in the system's image viewer when clicked.
Item {
    id: shot

    property url source
    property string caption: ""
    property int pixelWidth: 0
    property int pixelHeight: 0
    readonly property real ratio: pixelWidth > 0 && pixelHeight > 0 ? pixelHeight / pixelWidth : 0.625
    readonly property real imageH: Math.max(40, Math.round(width * ratio))

    implicitHeight: imageH + (caption.length > 0 ? capText.implicitHeight + 6 : 0)
    height: implicitHeight

    // Arrives a beat after the answer settles: fade, rise and a touch of scale.
    property real appear: 0
    opacity: appear
    scale: 0.97 + 0.03 * appear
    transformOrigin: Item.Top
    transform: Translate { y: (1 - shot.appear) * 8 }
    Component.onCompleted: appear = 1
    Behavior on appear { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }

    Rectangle {
        id: frame
        width: parent.width
        height: shot.imageH
        radius: 10
        color: Theme.tile
        border.width: 1
        border.color: hit.containsMouse ? Theme.accent : Theme.borderSoft
        Behavior on border.color { ColorAnimation { duration: 140 } }
        clip: true

        Image {
            id: img
            anchors.fill: parent
            anchors.margins: 1
            source: shot.source
            sourceSize.width: Math.round(shot.width * 2)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 260 } }
        }

        Glyph {
            anchors.centerIn: parent
            visible: img.status !== Image.Ready
            name: "eye"
            size: 20
            color: Theme.textFaint
        }

        // "Open" cue on hover.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            width: 26
            height: 26
            radius: 13
            color: Qt.rgba(0, 0, 0, 0.55)
            opacity: hit.containsMouse ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 140 } }
            Glyph {
                anchors.centerIn: parent
                name: "expand"
                size: 13
                color: "#ffffff"
            }
        }
    }

    Text {
        id: capText
        anchors.top: frame.bottom
        anchors.topMargin: 6
        width: parent.width
        visible: shot.caption.length > 0
        text: shot.caption
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    MouseArea {
        id: hit
        anchors.fill: frame
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: shot.caption.length > 0 ? qsTr("Screenshot: %1. Open full size").arg(shot.caption)
                                                 : qsTr("Screenshot. Open full size")
        onClicked: Qt.openUrlExternally(shot.source)
    }
}
