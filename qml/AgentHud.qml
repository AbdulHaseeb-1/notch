import QtQuick

// The pill under the notch while Buddy drives the screen: what it is doing,
// a Stop button, and how to take back control. Paused (because the user moved
// the mouse or pressed a key) it says so and offers Resume.
Rectangle {
    id: hud

    property bool paused: false
    property string pauseReason: ""
    property string actionLabel: ""
    signal stopClicked()
    signal resumeClicked()

    readonly property real textWidth: Math.min(340, Math.max(titleMetrics.advanceWidth, subMetrics.advanceWidth) + 2)
    implicitWidth: 18 + 12 + 12 + textWidth + 12 + buttons.implicitWidth + 9
    implicitHeight: 54
    radius: height / 2
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.surfaceAlpha)
    border.width: 1
    border.color: hud.paused ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45) : Theme.glassBorderStrong

    Accessible.role: Accessible.AlertMessage
    Accessible.name: titleText.text + ". " + subText.text

    TextMetrics { id: titleMetrics; text: titleText.text; font: titleText.font }
    TextMetrics { id: subMetrics; text: subText.text; font: subText.font }

    Row {
        id: body
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        // Live dot: a steady pulse while in control, amber and still when paused.
        Item {
            width: 12
            height: 12
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                id: halo
                anchors.centerIn: parent
                width: 12
                height: 12
                radius: 6
                color: hud.paused ? Theme.warning : Theme.accent
                visible: !hud.paused
                SequentialAnimation on scale {
                    running: !hud.paused && hud.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 2.1; duration: 1300; easing.type: Easing.OutCubic }
                }
                SequentialAnimation on opacity {
                    running: !hud.paused && hud.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.45; to: 0; duration: 1300; easing.type: Easing.OutCubic }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: hud.paused ? Theme.warning : Theme.accent
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: hud.textWidth
            spacing: 2

            Text {
                id: titleText
                width: parent.width
                text: hud.paused ? qsTr("Buddy paused. You have control") : qsTr("Buddy is controlling your screen")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                id: subText
                width: parent.width
                text: hud.paused
                      ? (hud.pauseReason.length > 0 ? hud.pauseReason : qsTr("Resume when you're ready"))
                      : (hud.actionLabel.length > 0 ? hud.actionLabel : qsTr("Working…"))
                color: hud.paused ? Theme.warning : Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontSm
                elide: Text.ElideRight
            }
        }

        Row {
            id: buttons
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Text {
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: 4
                text: qsTr("Ctrl+Alt+Esc")
                color: Theme.textFaint
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontXs
            }

            Rectangle {
                visible: hud.paused
                anchors.verticalCenter: parent.verticalCenter
                width: resumeLabel.implicitWidth + 24
                height: 34
                radius: 17
                color: resumeArea.containsMouse ? Theme.hover : Theme.tile
                border.width: 1
                border.color: Theme.tileEdge
                Accessible.role: Accessible.Button
                Accessible.name: resumeLabel.text
                Text {
                    id: resumeLabel
                    anchors.centerIn: parent
                    text: qsTr("Resume")
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontBase
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: resumeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hud.resumeClicked()
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: stopRow.implicitWidth + 26
                height: 36
                radius: 18
                color: stopArea.pressed ? Qt.darker(Theme.danger, 1.15)
                     : stopArea.containsMouse ? Qt.lighter(Theme.danger, 1.08) : Theme.danger
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Stop Buddy")
                Row {
                    id: stopRow
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9
                        height: 9
                        radius: 2
                        color: Theme.inverse
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Stop")
                        color: Theme.inverse
                        font.family: Theme.font
                        font.pixelSize: Theme.fontBase
                        font.weight: Font.Bold
                    }
                }
                MouseArea {
                    id: stopArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hud.stopClicked()
                }
            }
        }
    }
}
