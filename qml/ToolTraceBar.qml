import QtQuick

// Summary bar above an answer that used agent tools:
//   [icon][icon][icon]  4 steps · 3.3s · 1 failed              Trace  ⌄
// Clicking toggles the step list. Transparent glass, stone ink.
Rectangle {
    id: bar
    property var steps: []
    property var icons: []
    property real durationMs: 0
    property bool expanded: false
    signal toggled()

    readonly property int failed: {
        let n = 0
        for (let i = 0; i < (steps ? steps.length : 0); ++i)
            if (steps[i].status === "error" || steps[i].status === "denied") ++n
        return n
    }

    implicitHeight: 30
    height: implicitHeight
    radius: Theme.radiusMd
    color: hit.containsMouse ? Theme.glassFillHover : Theme.glassFill
    border.width: 1
    border.color: expanded ? Theme.glassBorderStrong : Theme.glassBorder

    Row {
        id: lead
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Repeater {
            model: bar.icons
            Glyph {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                name: String(modelData)
                size: 13
                color: Theme.textDim
            }
        }
    }

    Text {
        anchors.left: lead.right
        anchors.leftMargin: 9
        anchors.right: tail.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: {
            const n = bar.steps ? bar.steps.length : 0
            let s = n === 1 ? qsTr("1 step") : qsTr("%1 steps").arg(n)
            const d = Theme.formatDuration(bar.durationMs)
            if (d.length) s += "  ·  " + d
            if (bar.failed > 0) s += "  ·  " + qsTr("%1 failed").arg(bar.failed)
            return s
        }
        color: bar.failed > 0 ? Theme.inkSoft : Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    Row {
        id: tail
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.expanded ? qsTr("Hide") : qsTr("Trace")
            color: hit.containsMouse ? Theme.ink : Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            name: "down"
            size: 12
            color: hit.containsMouse ? Theme.ink : Theme.textFaint
            rotation: bar.expanded ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: bar.expanded ? qsTr("Hide tool trace") : qsTr("Show tool trace")
        onClicked: bar.toggled()
    }
}
