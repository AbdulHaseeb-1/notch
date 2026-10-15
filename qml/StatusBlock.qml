import QtQuick

// Clock block in the notch, with optional CPU, memory and battery figures
// beside it (Settings > System stats). Click opens the system panel,
// right-click opens settings.
Item {
    id: block

    property real s: 1
    property bool showClock: true
    property bool showStats: false
    property bool active: false
    property real appear: 1

    signal clicked()
    signal settingsRequested()
    signal hintRequested(string text)
    signal hintCleared()

    Theme { id: t }

    visible: showClock || showStats
    width: visible ? row.implicitWidth + Math.round(20 * s) : 0
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

    // One "label value" line of the stats column.
    component StatLine: Row {
        id: line
        property string label
        property string value
        property bool warn: false
        spacing: Math.round(5 * block.s)

        Text {
            width: Math.round(26 * block.s)
            text: line.label
            color: t.textDim
            font.family: t.font
            font.pixelSize: Math.round(10 * block.s)
            font.weight: Font.DemiBold
        }
        Text {
            text: line.value
            color: line.warn ? t.danger : t.ink
            font.family: t.font
            font.pixelSize: Math.round(11.5 * block.s)
            font.weight: Font.Bold
            font.features: ({ "tnum": 1 })
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: parent.height - Math.round(14 * block.s)
        radius: Math.round(10 * block.s)
        color: block.active ? t.hover : t.raised
        opacity: block.active || hit.containsMouse ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on color { ColorAnimation { duration: 140 } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Math.round(12 * block.s)

        Column {
            id: stats
            visible: block.showStats
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            StatLine {
                label: "CPU"
                value: monitor.cpuPercent < 0 ? "—" : monitor.cpuPercent + "%"
                warn: monitor.cpuPercent >= 90
            }
            StatLine {
                label: "RAM"
                value: monitor.memoryTotal > 0 ? monitor.memoryPercent + "%" : "—"
                warn: monitor.memoryPercent >= 90
            }
            StatLine {
                visible: monitor.hasBattery
                label: "BAT"
                value: monitor.batteryPercent < 0 ? "—" : monitor.batteryPercent + "%"
                warn: monitor.batteryPercent >= 0 && monitor.batteryPercent <= 15 && !monitor.charging
            }
        }

        Column {
            id: clockColumn
            visible: block.showClock
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                anchors.right: parent.right
                text: block.now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                color: t.ink
                font.family: t.font
                font.pixelSize: Math.round(13 * block.s)
                font.weight: Font.Bold
                font.features: ({ "tnum": 1 })
            }

            Text {
                anchors.right: parent.right
                text: block.now.toLocaleDateString(Qt.locale(), "M/d/yyyy")
                color: t.text
                font.family: t.font
                font.pixelSize: Math.round(12.5 * block.s)
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
                block.hintRequested(block.dateLine + qsTr("  ·  timer, clipboard, system"))
            else
                block.hintCleared()
        }
    }
}
