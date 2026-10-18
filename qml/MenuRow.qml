import QtQuick

// One row of a context menu / settings action list.
Item {
    id: row
    property string label
    property string icon
    property string hint
    property bool danger: false
    signal triggered()


    implicitHeight: 38
    implicitWidth: 200

    readonly property color fg: danger ? Theme.danger : (hit.containsMouse ? Theme.ink : Theme.text)

    Glyph {
        id: rowGlyph
        visible: row.icon.length > 0
        name: row.icon
        size: 16
        color: row.fg
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        anchors.left: rowGlyph.visible ? rowGlyph.right : parent.left
        anchors.leftMargin: rowGlyph.visible ? 12 : 16
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: row.fg
        font.family: Theme.font
        font.pixelSize: 14
        font.weight: Font.DemiBold
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: row.hint
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: 12
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.triggered()
    }
}
