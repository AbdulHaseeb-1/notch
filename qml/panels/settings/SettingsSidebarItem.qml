pragma ComponentBehavior: Bound
import QtQuick

// One entry of the Settings sidebar.
Item {
    id: item
    property string text: ""
    property string icon: "settings"
    property string badge: ""
    property bool selected: false
    signal clicked()

    width: parent ? parent.width : 190
    height: 36
    Accessible.role: Accessible.PageTab
    Accessible.name: text

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: item.selected ? Theme.tileActive : (hit.containsMouse ? Theme.glassFillHover : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: 0
        width: 3
        height: item.selected ? 16 : 6
        radius: 1.5
        color: Theme.accent
        opacity: item.selected ? 1 : 0
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
    Glyph {
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        name: item.icon
        size: 16
        color: item.selected ? Theme.ink : (hit.containsMouse ? Theme.inkSoft : Theme.textDim)
    }
    Text {
        x: 40
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 40 - (badgeText.visible ? badgeText.implicitWidth + 22 : 12)
        text: item.text
        elide: Text.ElideRight
        color: item.selected ? Theme.ink : (hit.containsMouse ? Theme.inkSoft : Theme.textDim)
        font.family: Theme.font
        font.pixelSize: Theme.fontMd
        font.weight: item.selected ? Font.DemiBold : Font.Medium
    }
    Text {
        id: badgeText
        visible: item.badge.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: item.badge
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: item.clicked()
    }
}
