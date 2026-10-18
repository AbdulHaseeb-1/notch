import QtQuick

// Human-in-the-loop approval for one paused tool call
// (Claude Agent SDK canUseTool / OpenAI Agents SDK needsApproval):
//
//   [shield]  Allow Buddy to run this?                         1:54
//             BASH · Run `npm install`
//   ┌ mono detail ───────────────────────────────────────────────────┐
//   reason from the policy
//                                    [Deny]  [Always allow]  [Allow once]
//
// `request` is BuddyProvider.pendingApproval. Emits decided(approve, always).
// Enter = allow once, Esc = deny when the card has focus.
Rectangle {
    id: card
    property var request: ({})
    signal decided(bool approve, bool always)

    readonly property int secondsLeft: Number(request.secondsLeft || 0)
    readonly property bool urgent: secondsLeft > 0 && secondsLeft <= 20

    implicitHeight: col.implicitHeight + 22
    height: implicitHeight
    radius: Theme.radiusMd
    color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.07)
    border.width: 1
    border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.38)
    focus: visible

    Keys.onReturnPressed: card.decided(true, false)
    Keys.onEnterPressed: card.decided(true, false)
    Keys.onEscapePressed: card.decided(false, false)

    function clock(sec) {
        const s = Math.max(0, sec)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    component ActBtn: Rectangle {
        id: b
        property string label
        property string icon
        property bool primary: false
        signal clicked()
        width: lbl.implicitWidth + (icon.length ? 34 : 22)
        height: 28
        radius: Theme.radiusSm + 1
        color: primary ? (ma.pressed ? Theme.stone200 : ma.containsMouse ? Theme.stone50 : Theme.stone100)
                       : (ma.pressed ? Theme.glassFillActive : ma.containsMouse ? Theme.glassFillHover : Theme.glassFill)
        border.width: primary ? 0 : 1
        border.color: Theme.glassBorderStrong
        scale: ma.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Row {
            anchors.centerIn: parent
            spacing: 5
            Glyph {
                visible: b.icon.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: b.icon
                size: 12
                color: b.primary ? Theme.inverse : Theme.inkSoft
            }
            Text {
                id: lbl
                anchors.verticalCenter: parent.verticalCenter
                text: b.label
                color: b.primary ? Theme.inverse : Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
            }
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: b.clicked()
        }
    }

    Column {
        id: col
        x: 12
        y: 11
        width: parent.width - 24
        spacing: 8

        Item {
            width: parent.width
            height: 30

            Rectangle {
                id: shield
                width: 28
                height: 28
                radius: Theme.radiusSm + 2
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
                Glyph {
                    anchors.centerIn: parent
                    name: "shield-alert"
                    size: 15
                    color: Theme.warning
                }
                // A slow, cheap breathing cue so the card reads as "waiting on you".
                SequentialAnimation on opacity {
                    running: card.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.6; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Column {
                anchors.left: shield.right
                anchors.leftMargin: 10
                anchors.right: timer.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: qsTr("Allow Buddy to run this?")
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Row {
                    width: parent.width
                    spacing: 6
                    Glyph {
                        anchors.verticalCenter: parent.verticalCenter
                        name: String(card.request.icon || "terminal")
                        size: 11
                        color: Theme.textDim
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(card.request.badge || card.request.tool || "TOOL")
                        color: Theme.textFaint
                        font.family: Theme.font
                        font.pixelSize: Theme.fontXs - 1
                        font.weight: Font.Bold
                        font.letterSpacing: 0.6
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 80
                        text: String(card.request.title || "")
                        color: Theme.inkSoft
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                id: timer
                anchors.right: parent.right
                anchors.top: parent.top
                text: card.secondsLeft > 0 ? card.clock(card.secondsLeft) : ""
                color: card.urgent ? Theme.warning : Theme.textFaint
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontXs
            }
        }

        Rectangle {
            width: parent.width
            visible: detailText.text.length > 0
            height: detailText.implicitHeight + 12
            radius: Theme.radiusSm
            color: Theme.glassDeep
            border.width: 1
            border.color: Theme.glassBorder
            Text {
                id: detailText
                x: 8
                y: 6
                width: parent.width - 16
                text: String(card.request.detail || "")
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontXs + 0.5
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 5
                elide: Text.ElideRight
            }
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: String(card.request.reason || "")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontXs
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Row {
            anchors.right: parent.right
            spacing: 6

            ActBtn { label: qsTr("Deny"); icon: "close"; onClicked: card.decided(false, false) }
            ActBtn { label: qsTr("Always allow"); onClicked: card.decided(true, true) }
            ActBtn { label: qsTr("Allow once"); icon: "check"; primary: true; onClicked: card.decided(true, false) }
        }
    }
}
