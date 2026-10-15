import QtQuick

// CPU, memory, battery, disk and throughput: the System page of the hub.
Item {
    id: panel

    signal closeRequested()

    Theme { id: t }

    function fmtBytes(n) {
        if (!n || n <= 0)
            return "—"
        const gb = n / 1073741824
        if (gb >= 10)
            return gb.toFixed(gb >= 100 ? 0 : 1) + " GB"
        if (gb >= 1)
            return gb.toFixed(1) + " GB"
        return Math.max(1, Math.round(n / 1048576)) + " MB"
    }

    function fmtRate(bps) {
        if (bps < 1024)
            return Math.round(bps) + " B/s"
        if (bps < 1024 * 100)
            return (bps / 1024).toFixed(1) + " KB/s"
        if (bps < 1048576)
            return Math.round(bps / 1024) + " KB/s"
        if (bps < 1048576 * 10)
            return (bps / 1048576).toFixed(1) + " MB/s"
        return Math.round(bps / 1048576) + " MB/s"
    }

    function fmtSpan(sec) {
        sec = Math.max(0, Math.floor(sec))
        const d = Math.floor(sec / 86400)
        const h = Math.floor((sec % 86400) / 3600)
        const m = Math.floor((sec % 3600) / 60)
        if (d > 0)
            return d + "d " + h + "h"
        if (h > 0)
            return h + "h " + m + "m"
        return m + "m"
    }

    readonly property string batteryValue: {
        if (monitor.batteryPercent < 0)
            return "—"
        let text = monitor.batteryPercent + "%"
        if (monitor.charging)
            text += "  ·  Charging"
        else if (monitor.pluggedIn)
            text += "  ·  Plugged in"
        else if (monitor.batterySeconds > 0)
            text += "  ·  " + fmtSpan(monitor.batterySeconds) + " left"
        return text
    }

    component Meter: Column {
        id: meter
        Theme { id: theme }
        property string label
        property string value
        property real ratio: 0
        property bool warn: false
        spacing: 7

        Item {
            width: parent.width
            height: 16
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: meter.label
                color: theme.textDim
                font.family: theme.font
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: meter.value
                color: meter.warn ? theme.danger : theme.ink
                font.family: theme.font
                font.pixelSize: 13
                font.weight: Font.Bold
                font.features: ({ "tnum": 1 })
            }
        }
        Item {
            width: parent.width
            height: 5
            Rectangle {
                anchors.fill: parent
                radius: 2.5
                color: theme.track
            }
            Rectangle {
                width: Math.max(0, Math.min(1, meter.ratio)) * parent.width
                height: parent.height
                radius: 2.5
                color: meter.warn ? theme.danger : theme.ink
                Behavior on width { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 200 } }
            }
        }
    }

    implicitWidth: 348
    implicitHeight: body.implicitHeight

    Column {
        id: body
        width: parent.width

        Item {
            width: parent.width
            height: metrics.implicitHeight
        Column {
            id: metrics
            x: 20
            width: parent.width - 40
            topPadding: 18
            bottomPadding: 8
            spacing: 16

            Meter {
                width: parent.width
                label: "CPU"
                value: monitor.cpuPercent < 0 ? "—" : monitor.cpuPercent + "%"
                ratio: monitor.cpuPercent < 0 ? 0 : monitor.cpuPercent / 100
                warn: monitor.cpuPercent >= 90
            }

            // Live CPU history mini bar chart
            Row {
                width: parent.width
                height: 22
                spacing: 3
                Repeater {
                    model: monitor.cpuHistory
                    delegate: Item {
                        required property int index
                        required property var modelData
                        width: (parent.width - 27 * 3) / 28
                        height: parent.height
                        Rectangle {
                            width: parent.width
                            height: Math.max(2, Math.round((Number(modelData) / 100) * parent.height))
                            anchors.bottom: parent.bottom
                            radius: 1
                            color: Number(modelData) >= 90 ? t.danger : t.ink
                            opacity: 0.25 + 0.7 * (index / 27)
                            Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutQuad } }
                        }
                    }
                }
            }
            Meter {
                width: parent.width
                label: "Memory"
                value: fmtBytes(monitor.memoryUsed) + "  /  " + fmtBytes(monitor.memoryTotal)
                ratio: monitor.memoryPercent / 100
                warn: monitor.memoryPercent >= 90
            }
            Meter {
                visible: monitor.hasBattery
                width: parent.width
                label: "Battery"
                value: panel.batteryValue
                ratio: monitor.batteryPercent < 0 ? 0 : monitor.batteryPercent / 100
                warn: monitor.batteryPercent >= 0 && monitor.batteryPercent <= 15 && !monitor.charging
            }
            Meter {
                visible: monitor.diskTotal > 0
                width: parent.width
                label: monitor.diskName.length > 0 ? "Disk  " + monitor.diskName : "Disk"
                value: fmtBytes(monitor.diskUsed) + "  /  " + fmtBytes(monitor.diskTotal)
                ratio: monitor.diskPercent / 100
                warn: monitor.diskPercent >= 92
            }

            Column {
                visible: monitor.netReady
                width: parent.width
                spacing: 8
                Text {
                    text: "Network"
                    color: t.textDim
                    font.family: t.font
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Row {
                    width: parent.width
                    spacing: 18
                    Text {
                        text: "Down  " + panel.fmtRate(monitor.downBps)
                        color: t.ink
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "Up  " + panel.fmtRate(monitor.upBps)
                        color: t.ink
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                }
            }
        }
        }

        Rectangle { width: parent.width; height: 1; color: t.borderSoft }

        Item {
            width: parent.width
            height: 46
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "Uptime  ·  " + panel.fmtSpan(monitor.uptimeSeconds)
                color: t.textDim
                font.family: t.font
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: tmRow.implicitWidth + 18
                height: 28
                radius: 14
                color: tmHit.containsMouse ? t.raised : "transparent"
                border.width: 1
                border.color: t.border
                Row {
                    id: tmRow
                    anchors.centerIn: parent
                    spacing: 6
                    Glyph {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "activity"
                        size: 13
                        color: tmHit.containsMouse ? t.ink : t.text
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Task Manager"
                        color: tmHit.containsMouse ? t.ink : t.text
                        font.family: t.font
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }
                MouseArea {
                    id: tmHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        monitor.openTaskManager()
                        panel.closeRequested()
                    }
                }
            }
        }
    }
}
