import QtQuick

// One line of the live agent tool timeline (shown while Buddy is working):
//   [status] [tool icon] BADGE  Title …                      1.2s
// Transparent glass row; only the running step is lifted with a faint fill so
// the eye lands on what is happening now.
Item {
    id: row
    property var step: ({})
    implicitHeight: 28
    height: implicitHeight

    readonly property string status: String(step.status || "done")
    readonly property bool isRunning: status === "running"
    readonly property bool isError: status === "error"
    readonly property bool isAttention: status === "pending" || status === "denied"

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm + 1
        color: row.isRunning ? Theme.glassFillActive
             : row.isError ? Theme.dangerSoft
             : row.isAttention ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.08) : "transparent"
    }

    ToolStatusIcon {
        id: st
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        status: row.status
        size: 13
    }

    Glyph {
        id: ic
        anchors.left: st.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        name: String(row.step.icon || "terminal")
        size: 13
        color: row.isRunning ? Theme.ink : Theme.textDim
    }

    Text {
        id: badge
        anchors.left: ic.right
        anchors.leftMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        text: String(row.step.badge || "TOOL")
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fontXs - 1
        font.weight: Font.Bold
        font.letterSpacing: 0.6
    }

    Text {
        anchors.left: badge.right
        anchors.leftMargin: 7
        anchors.right: meta.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: String(row.step.title || "")
        color: row.isRunning ? Theme.ink : (row.isError ? Theme.danger : Theme.inkSoft)
        font.family: Theme.font
        font.pixelSize: Theme.fontSm
        font.weight: row.isRunning ? Font.DemiBold : Font.Medium
        elide: Text.ElideRight
    }

    Text {
        id: meta
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: row.isRunning ? qsTr("running")
            : row.status === "denied" ? qsTr("not run")
            : row.status === "pending" ? qsTr("waiting")
            : row.status === "skipped" ? qsTr("skipped")
            : row.isError ? qsTr("failed")
            : (Theme.formatDuration(row.step.durationMs) || qsTr("done"))
        color: row.isError ? Theme.danger : row.isAttention ? Theme.warning : Theme.textFaint
        font.family: Theme.monoFont
        font.pixelSize: Theme.fontXs
    }
}
