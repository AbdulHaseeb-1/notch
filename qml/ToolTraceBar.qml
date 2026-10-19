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

    // Undo for the files the run changed. `undoSource` is the agent (anything
    // with lastRunChanges and undoLastRun(force)); without one there is no button.
    property var undoSource: null
    readonly property var changes: undoSource && undoSource.lastRunChanges ? undoSource.lastRunChanges : ({})
    readonly property int changedCount: changes && changes.count ? Number(changes.count) : 0
    // What the last undo said ("Restored 2 files."), and whether some files were left alone.
    property string undoNote: ""
    property bool undoLeftFiles: false
    function undo() {
        if (!undoSource)
            return
        const r = undoSource.undoLastRun(undoLeftFiles)
        undoNote = String(r.message || "")
        undoLeftFiles = !!(r.conflicts && r.conflicts.length > 0)
    }

    readonly property int failed: {
        let n = 0
        for (let i = 0; i < (steps ? steps.length : 0); ++i)
            if (steps[i].status === "error") ++n
        return n
    }
    readonly property int blocked: {
        let n = 0
        for (let i = 0; i < (steps ? steps.length : 0); ++i)
            if (steps[i].status === "denied") ++n
        return n
    }

    readonly property int skipped: {
        let n = 0
        for (let i = 0; i < (steps ? steps.length : 0); ++i)
            if (steps[i].status === "skipped") ++n
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
        anchors.right: undoChip.visible ? undoChip.left : tail.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: {
            if (bar.undoNote.length > 0 && bar.changedCount === 0)
                return bar.undoNote
            const n = bar.steps ? bar.steps.length : 0
            let s = n === 1 ? qsTr("1 step") : qsTr("%1 steps").arg(n)
            const d = Theme.formatDuration(bar.durationMs)
            if (d.length) s += "  ·  " + d
            if (bar.failed > 0) s += "  ·  " + qsTr("%1 failed").arg(bar.failed)
            if (bar.blocked > 0) s += "  ·  " + qsTr("%1 not run").arg(bar.blocked)
            if (bar.skipped > 0) s += "  ·  " + qsTr("%1 skipped").arg(bar.skipped)
            return s
        }
        color: bar.failed + bar.blocked > 0 ? Theme.inkSoft : Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    // "Undo 3 files": puts back what this run changed. If you changed some of
    // them since, they are left alone and the button offers to override that.
    Rectangle {
        id: undoChip
        objectName: "undoChip"
        visible: bar.undoSource !== null && bar.changedCount > 0
        z: 1          // above the bar's own click area, which is declared after it
        anchors.right: tail.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        height: 20
        width: undoLabel.implicitWidth + 16
        radius: 10
        color: undoHit.pressed ? Theme.press : (undoHit.containsMouse ? Theme.hover : "transparent")
        border.width: 1
        border.color: Theme.glassBorderStrong
        Text {
            id: undoLabel
            anchors.centerIn: parent
            text: bar.undoLeftFiles ? qsTr("Undo anyway")
                  : (bar.changedCount === 1 ? qsTr("Undo 1 file") : qsTr("Undo %1 files").arg(bar.changedCount))
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: undoHit
            objectName: "undoHit"
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: undoLabel.text
            onClicked: bar.undo()
        }
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
