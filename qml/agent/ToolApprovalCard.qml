import QtQuick

// Human-in-the-loop approval for one paused tool call
// (Claude Agent SDK canUseTool / OpenAI Agents SDK needsApproval):
//
//   ● Allow Buddy to run this?                                 1:54
//     BASH · Run `npm install`
//   mono detail (recessed, no outline)
//   reason from the policy
//                                    Deny   Always allow  [Allow once]
//
// Flat and dark: no outlines, a single quiet dot instead of a warning badge,
// text buttons with one light primary.

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
    color: Qt.rgba(1, 1, 1, 0.035)
    border.width: 0
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
        property bool primary: false
        property bool keyed: false
        signal clicked()
        width: lbl.implicitWidth + (primary ? 26 : 20)
        height: 28
        radius: height / 2
        color: primary ? (ma.pressed ? Theme.stone200 : ma.containsMouse || keyed ? Theme.stone50 : Theme.stone100)
                       : (ma.pressed ? Theme.glassFillActive
                          : ma.containsMouse || keyed ? Theme.glassFillHover : "transparent")
        Behavior on color { ColorAnimation { duration: 110 } }
        Accessible.role: Accessible.Button
        Accessible.name: b.label
        scale: ma.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Text {
            id: lbl
            anchors.centerIn: parent
            text: b.label
            color: b.primary ? Theme.inverse : (b.keyed || ma.containsMouse ? Theme.ink : Theme.textDim)
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
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

            // One quiet dot that slowly breathes: "waiting on you".
            Rectangle {
                id: shield
                width: 6
                height: 6
                radius: 3
                x: 2
                anchors.verticalCenter: parent.verticalCenter
                color: card.urgent ? Theme.warning : Theme.textDim
                SequentialAnimation on opacity {
                    running: card.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 1100; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 1100; easing.type: Easing.InOutSine }
                }
            }

            Column {
                anchors.left: shield.right
                anchors.leftMargin: 12
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
            spacing: 2

            ActBtn { label: qsTr("Deny"); keyed: card.focusIdx === 0; onClicked: card.decided(false, false) }
            ActBtn { label: qsTr("Always allow"); keyed: card.focusIdx === 1; onClicked: card.decided(true, true) }
            ActBtn { label: qsTr("Allow once"); primary: true; keyed: card.focusIdx === 2; onClicked: card.decided(true, false) }
        }
    }
}
