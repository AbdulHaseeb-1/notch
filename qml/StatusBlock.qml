import QtQuick

// Clock block in the notch. Click opens the hub, right-click opens settings.
Item {
    id: block

    property real s: 1
    property bool showClock: true
    property bool active: false
    property real appear: 1

    signal clicked()
    signal settingsRequested()
    signal hintRequested(string text)
    signal hintCleared()


    visible: showClock
    width: visible ? row.implicitWidth + Math.round(18 * s) : 0
    height: parent ? parent.height : Math.round(50 * s)
    opacity: appear

    Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    property date now: new Date()
    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: block.now = new Date()
    }

    readonly property string dateLine: now.toLocaleDateString(Qt.locale(), "dddd, d MMMM")

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Math.round(10 * block.s)
        scale: hit.pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

        Column {
            id: clockColumn
            visible: block.showClock
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                anchors.right: parent.right
                text: block.now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Math.round(13 * block.s)
                font.weight: Font.Bold
                font.features: ({ "tnum": 1 })
            }

            Text {
                anchors.right: parent.right
                text: block.now.toLocaleDateString(Qt.locale(), "ddd, MMM d")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Math.round(10.5 * block.s)
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                block.settingsRequested()
            else
                block.clicked()
        }
        onContainsMouseChanged: {
            if (containsMouse)
                block.hintRequested(block.dateLine + qsTr("  ·  timer, clipboard"))
            else
                block.hintCleared()
        }
    }
}
