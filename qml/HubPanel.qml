import QtQuick

// The hub under the clock: System, Timer and Clipboard in one card. A tab
// strip on top; the body slides between pages and springs to the new height.
Item {
    id: hub

    property int tab: 0                 // 0 System, 1 Timer, 2 Clipboard
    property var focusTimer             // FocusTimer
    readonly property real headerH: 58

    signal closeRequested()
    signal tabPicked(int tab)
    signal toastRequested(string text)

    Theme { id: t }

    implicitWidth: 348
    // The size the card is heading for, for the click-through mask: using the
    // animated height would rebuild the window mask on every frame.
    readonly property real targetHeight: headerH + 1 + pageHeight(tab)
    implicitHeight: headerH + 1 + content.height

    function pageHeight(i) {
        return i === 0 ? system.implicitHeight : (i === 1 ? timerPage.implicitHeight : clip.implicitHeight)
    }

    Item {
        id: header
        width: parent.width
        height: hub.headerH

        Segmented {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: closeBtn.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 34
            options: [
                { label: qsTr("System"), value: 0 },
                { label: qsTr("Timer"), value: 1 },
                { label: qsTr("Clipboard"), value: 2 }
            ]
            value: hub.tab
            onPicked: (v) => hub.tabPicked(v)
        }

        Item {
            id: closeBtn
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: t.hover
                opacity: closeHit.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
            Glyph {
                anchors.centerIn: parent
                name: "close"
                size: 15
                color: closeHit.containsMouse ? t.ink : t.textDim
            }
            MouseArea {
                id: closeHit
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: hub.closeRequested()
            }
        }
    }

    Rectangle {
        id: rule
        anchors.top: header.bottom
        width: parent.width
        height: 1
        color: t.borderSoft
    }

    Item {
        id: content
        anchors.top: rule.bottom
        width: parent.width
        height: hub.pageHeight(hub.tab)
        clip: true
        Behavior on height {
            SpringAnimation { spring: 4.2; damping: 0.5; epsilon: 0.3 }
        }

        SystemPanel {
            id: system
            width: parent.width
            x: hub.tab === 0 ? 0 : -28
            opacity: hub.tab === 0 ? 1 : 0
            visible: opacity > 0.01
            enabled: hub.tab === 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            onCloseRequested: hub.closeRequested()
        }

        TimerView {
            id: timerPage
            width: parent.width
            timer: hub.focusTimer
            x: hub.tab === 1 ? 0 : (hub.tab < 1 ? 28 : -28)
            opacity: hub.tab === 1 ? 1 : 0
            visible: opacity > 0.01
            enabled: hub.tab === 1
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            onToastRequested: (text) => hub.toastRequested(text)
        }

        ClipboardView {
            id: clip
            width: parent.width
            x: hub.tab === 2 ? 0 : 28
            opacity: hub.tab === 2 ? 1 : 0
            visible: opacity > 0.01
            enabled: hub.tab === 2
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            onToastRequested: (text) => hub.toastRequested(text)
        }
    }
}
