import QtQuick

// The hub under the clock: Timer and Clipboard in one card. A tab
// strip on top; the body slides between pages and springs to the new height.
Item {
    id: hub

    property int tab: 0                 // 0 Timer, 1 Clipboard
    property var focusTimer             // FocusTimer
    property real maxBodyHeight: 100000
    readonly property real headerH: 58

    signal closeRequested()
    signal tabPicked(int tab)
    signal toastRequested(string text)


    // Wider hub (timer + clipboard history), clamped to the screen.
    property real maxPanelWidth: 100000
    implicitWidth: Theme.fit(Theme.hubWidth, maxPanelWidth)
    // The size the card is heading for, for the click-through mask: using the
    // animated height would rebuild the window mask on every frame.
    readonly property real targetBodyHeight: Math.min(pageHeight(tab), maxBodyHeight)
    readonly property real targetHeight: headerH + 1 + targetBodyHeight
    implicitHeight: headerH + 1 + content.height

    function pageHeight(i) {
        return i === 0 ? timerPage.implicitHeight : clip.implicitHeight
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
                { label: qsTr("Timer"), value: 0 },
                { label: qsTr("Clipboard"), value: 1 }
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
            Glyph {
                anchors.centerIn: parent
                name: "close"
                size: 15
                color: closeHit.containsMouse ? Theme.ink : Theme.textDim
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
        color: Theme.borderSoft
    }

    Item {
        id: content
        anchors.top: rule.bottom
        width: parent.width
        height: hub.targetBodyHeight
        clip: true
        Behavior on height {
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }

        TimerView {
            id: timerPage
            width: parent.width
            timer: hub.focusTimer
            x: hub.tab === 0 ? 0 : -20
            opacity: hub.tab === 0 ? 1 : 0
            visible: opacity > 0.01
            enabled: hub.tab === 0
            z: hub.tab === 0 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            onToastRequested: (text) => hub.toastRequested(text)
        }

        ClipboardView {
            id: clip
            width: parent.width
            x: hub.tab === 1 ? 0 : 20
            opacity: hub.tab === 1 ? 1 : 0
            visible: opacity > 0.01
            enabled: hub.tab === 1
            z: hub.tab === 1 ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            onToastRequested: (text) => hub.toastRequested(text)
        }
    }
}
