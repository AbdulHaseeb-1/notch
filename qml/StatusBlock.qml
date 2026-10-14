import QtQuick

// Clock block in the notch. Click opens the system panel, right-click opens
// settings.
Item {
    id: block

    property real s: 1
    property bool showClock: true
    property bool showStats: true   // kept for API compat, no longer used
    property bool active: false
    property real appear: 1

    signal clicked()
    signal settingsRequested()
    signal hintRequested(string text)
    signal hintCleared()

    Theme { id: t }

    visible: showClock
    width: visible ? col.implicitWidth + Math.round(20 * s) : 0
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

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: parent.height - Math.round(14 * block.s)
        radius: Math.round(10 * block.s)
        color: block.active ? "#1d1f24" : "#16181c"
        opacity: block.active || hit.containsMouse ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on color { ColorAnimation { duration: 140 } }
    }

    Column {
        id: col
        anchors.centerIn: parent
        spacing: 0

        Text {
            anchors.right: parent.right
            text: block.now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
            color: t.white
            font.family: t.font
            font.pixelSize: Math.round(13 * block.s)
            font.weight: Font.Bold
            font.features: ({ "tnum": 1 })
        }

        Text {
            anchors.right: parent.right
            text: block.now.toLocaleDateString(Qt.locale(), "M/d/yyyy")
            color: "#d9dcdf"
            font.family: t.font
            font.pixelSize: Math.round(12.5 * block.s)
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
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
                block.hintRequested(block.dateLine)
            else
                block.hintCleared()
        }
    }
}
