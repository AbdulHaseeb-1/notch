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
    activeFocusOnTab: true
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Allow Buddy to run this tool?")
    Accessible.description: String(request.title || "")

    Keys.onReturnPressed: (event) => { card.decided(true, false); event.accepted = true }
    Keys.onEnterPressed: (event) => { card.decided(true, false); event.accepted = true }
    Keys.onEscapePressed: (event) => { card.decided(false, false); event.accepted = true }
    Keys.onLeftPressed: (event) => { focusDeny(); event.accepted = true }
    Keys.onRightPressed: (event) => { focusAllow(); event.accepted = true }
    Keys.onTabPressed: (event) => { cycleFocus(1); event.accepted = true }
    Keys.onBacktabPressed: (event) => { cycleFocus(-1); event.accepted = true }
    Keys.onSpacePressed: (event) => {
        if (focusIdx === 0) card.decided(false, false)
        else if (focusIdx === 1) card.decided(true, true)
        else card.decided(true, false)
        event.accepted = true
    }

    property int focusIdx: 2   // 0 deny, 1 always, 2 allow
    function focusDeny() { focusIdx = 0 }
    function focusAllow() { focusIdx = 2 }
    function cycleFocus(d) { focusIdx = (focusIdx + d + 3) % 3 }
    onVisibleChanged: if (visible) { focusIdx = 2; forceActiveFocus() }

    function clock(sec) {
        const s = Math.max(0, sec)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    component ActBtn: Rectangle {
        id: b
        property string label
        property string icon
        property bool primary: false
        property bool keyed: false
        signal clicked()
        width: lbl.implicitWidth + (icon.length ? 34 : 22)
        height: 28
        radius: Theme.radiusSm + 1
        color: primary ? (ma.pressed ? Theme.stone200 : ma.containsMouse ? Theme.stone50 : Theme.stone100)
                       : (ma.pressed ? Theme.glassFillActive : ma.containsMouse ? Theme.glassFillHover : Theme.glassFill)
        border.width: (primary && !keyed) ? 0 : 1
        border.color: keyed ? Theme.accent : Theme.glassBorderStrong
        Accessible.role: Accessible.Button
        Accessible.name: b.label
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

        // What "Always allow" would really do: save this narrow rule, or (when
        // no safe rule exists) allow the tool until Noch is restarted.
        Text {
            width: parent.width
            visible: String(card.request.alwaysLabel || "").length > 0
            text: card.request.alwaysSaved
                  ? qsTr("“Always allow” saves: %1").arg(String(card.request.alwaysLabel))
                  : qsTr("“Always allow” covers %1").arg(String(card.request.alwaysLabel))
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontXs
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }

        Row {
            anchors.right: parent.right
            spacing: 6

            ActBtn { label: qsTr("Deny"); icon: "close"; keyed: card.focusIdx === 0; onClicked: card.decided(false, false) }
            ActBtn { label: qsTr("Always allow"); keyed: card.focusIdx === 1; onClicked: card.decided(true, true) }
            ActBtn { label: qsTr("Allow once"); icon: "check"; primary: true; keyed: card.focusIdx === 2; onClicked: card.decided(true, false) }
        }
    }
}
