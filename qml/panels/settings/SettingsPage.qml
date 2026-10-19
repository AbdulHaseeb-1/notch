pragma ComponentBehavior: Bound
import QtQuick

// One page of Settings: a title, a line saying what it is for, then sections
// (SettingsSectionHeader + SettingsGroupCard) stacked in a single comfortable
// column that scrolls when it is taller than the window.
Flickable {
    id: page

    property string title: ""
    property string subtitle: ""
    property real maxContentWidth: 640
    property real sideMargin: 28
    default property alias sections: sectionCol.data
    readonly property real contentW: Math.max(200, Math.min(maxContentWidth, width - 2 * sideMargin))

    contentWidth: width
    contentHeight: wrap.height + 36
    interactive: contentHeight > height + 1
    boundsBehavior: Flickable.StopAtBounds
    flickDeceleration: 3500
    clip: true

    Column {
        id: wrap
        x: Math.round((page.width - page.contentW) / 2)
        y: 22
        width: page.contentW
        spacing: 4

        Text {
            width: parent.width
            text: page.title
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: 22
            font.weight: Font.Bold
            visible: page.title.length > 0
        }
        Text {
            width: parent.width
            text: page.subtitle
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            visible: page.subtitle.length > 0
            bottomPadding: 6
        }
        Column {
            id: sectionCol
            width: parent.width
            spacing: 10
        }
    }

    // A thin scroll indicator that only shows while moving.
    Rectangle {
        visible: page.interactive
        x: page.width - width - 4
        y: 6 + page.visibleArea.yPosition * (page.height - 12)
        width: 4
        height: Math.max(24, page.visibleArea.heightRatio * (page.height - 12))
        radius: 2
        color: Theme.stone500
        opacity: page.moving || page.dragging ? 0.8 : 0
        Behavior on opacity { NumberAnimation { duration: 260 } }
    }
}
