import QtQuick

// One step of the agent execution trace, as a dark glass card:
//
//   [icon]  Title of the call …                 BADGE · 812ms  [status]
//           detail / arguments (mono, faint)
//   ┌ output well (mono) — click to expand / collapse ───────────────────┐
//
// Plain Rectangles and Text only (no layers or effects); the output well
// clamps to a few lines until clicked.
Rectangle {
    id: card
    property var step: ({})
    property int collapsedLines: 3
    property bool expanded: false
    signal continueRequested()

    readonly property string status: String(step.status || "done")
    readonly property bool isError: status === "error"
    readonly property bool isDenied: status === "denied"
    readonly property bool isRunning: status === "running"
    readonly property string detail: String(step.detail || "")
    readonly property string output: String(step.output || "")
    readonly property string policyNote: String(step.policyNote || "")
    readonly property bool isPending: status === "pending"
    readonly property bool canContinue: step.computerPaused === true
    readonly property bool isFlagged: String(step.flag || "") === "injection"
    // Computer-use steps carry a small JPEG data URL of the screen around
    // the action (a ring marks the target), so the trace shows what happened.
    readonly property string thumbnail: String(step.thumbnail || "")

    implicitHeight: inner.implicitHeight + 18
    height: implicitHeight
    radius: Theme.radiusMd
    color: isError ? Theme.dangerSoft
         : isDenied ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.07)
         : (cardHover.hovered ? Theme.glassFillHover : Theme.glassFill)
    border.width: 1
    border.color: isError ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.30)
                : (isPending || isDenied) ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.35)
                : isRunning ? Theme.glassBorderStrong : Theme.glassBorder

    HoverHandler { id: cardHover }

    Column {
        id: inner
        x: 10
        y: 9
        width: parent.width - 20
        spacing: 6

        Item {
            width: parent.width
            height: 20

            Rectangle {
                id: icBox
                width: 20
                height: 20
                radius: Theme.radiusSm
                color: Theme.glassFillActive
                anchors.verticalCenter: parent.verticalCenter
                Glyph {
                    anchors.centerIn: parent
                    name: String(card.step.icon || "terminal")
                    size: 12
                    color: Theme.inkSoft
                }
            }

            Text {
                anchors.left: icBox.right
                anchors.leftMargin: 8
                anchors.right: right.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: String(card.step.title || "")
                color: card.isError ? Theme.danger : card.isDenied ? Theme.textDim : Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Row {
                id: right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(card.step.badge || "TOOL")
                    color: Theme.textFaint
                    font.family: Theme.font
                    font.pixelSize: Theme.fontXs - 1
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: card.isRunning ? qsTr("running")
                        : card.isPending ? qsTr("needs approval")
                        : card.status === "denied" ? qsTr("not run")
                        : card.status === "skipped" ? qsTr("skipped")
                        : Theme.formatDuration(card.step.durationMs)
                    color: Theme.textFaint
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontXs
                }
                ToolStatusIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    status: card.status
                    size: 13
                }
            }
        }

        Text {
            width: parent.width
            visible: card.detail.length > 0
            leftPadding: 28
            text: card.detail
            color: Theme.textDim
            font.family: Theme.monoFont
            font.pixelSize: Theme.fontXs + 0.5
            wrapMode: Text.WrapAnywhere
            maximumLineCount: card.expanded ? 6 : 2
            elide: Text.ElideRight
        }

        // Policy / guardrail note: why a call was asked, approved, blocked or flagged.
        Row {
            visible: card.policyNote.length > 0
            leftPadding: 28
            spacing: 5
            width: parent.width
            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                name: card.isError || card.isDenied || card.isFlagged || card.isPending ? "shield-alert" : "shield-check"
                size: 11
                color: card.isError ? Theme.danger
                     : (card.isFlagged || card.isPending || card.isDenied) ? Theme.warning : Theme.textFaint
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 28 - 16
                text: card.policyNote
                color: card.isError ? Qt.lighter(Theme.danger, 1.1)
                     : (card.isFlagged || card.isPending || card.isDenied) ? Theme.warning : Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontXs
                elide: Text.ElideRight
            }
        }

        Rectangle {
            visible: card.thumbnail.length > 0
            x: 28
            width: Math.min(parent.width - 28, 240)
            height: visible ? thumb.paintedHeight + 2 : 0
            radius: Theme.radiusSm
            color: Theme.glassDeep
            border.width: 1
            border.color: Theme.glassBorder
            clip: true

            Image {
                id: thumb
                x: 1
                y: 1
                width: parent.width - 2
                source: card.thumbnail
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                Accessible.name: qsTr("Screen at this step")
            }
        }

        Rectangle {
            id: well
            width: parent.width
            visible: card.output.length > 0
            height: outText.implicitHeight + 12
            radius: Theme.radiusSm
            color: Theme.glassDeep
            border.width: 1
            border.color: Theme.glassBorder

            Text {
                id: outText
                x: 8
                y: 6
                width: parent.width - 16
                text: card.output
                color: card.isError ? Qt.lighter(Theme.danger, 1.1) : Theme.textDim
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontXs + 0.5
                lineHeight: 1.15
                wrapMode: Text.WrapAnywhere
                maximumLineCount: card.expanded ? 14 : card.collapsedLines
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: outText.truncated || card.expanded ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: outText.truncated || card.expanded
                onClicked: card.expanded = !card.expanded
            }
        }

        // Desktop control paused: the run ended with a failed action, but the
        // user can pick it back up without retyping the request. This lives in
        // the trace card because the floating HUD disappears when the run ends.
        Row {
            visible: card.canContinue
            width: parent.width
            leftPadding: 28
            spacing: 9

            Rectangle {
                id: continueChip
                objectName: "continueChip"
                anchors.verticalCenter: parent.verticalCenter
                width: continueRow.implicitWidth + 24
                height: 28
                radius: 14
                color: continueHit.pressed ? Theme.press
                     : continueHit.containsMouse ? Theme.hover : Theme.tile
                border.width: 1
                border.color: Theme.tileEdge

                Row {
                    id: continueRow
                    anchors.centerIn: parent
                    spacing: 6
                    Glyph {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "play"
                        size: 11
                        color: Theme.ink
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Continue")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                    }
                }

                MouseArea {
                    id: continueHit
                    objectName: "continueHit"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Continue after the desktop pause")
                    onClicked: card.continueRequested()
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Desktop control was paused")
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fontXs
                elide: Text.ElideRight
                width: Math.max(0, parent.width - parent.leftPadding - continueChip.width - parent.spacing)
            }
        }
    }
}
