import QtQuick

// Settings flyout under the notch. Two tabs (General / Apps), X-style tab
// strip with a white indicator.
Item {
    id: panel

    property var prefs
    property var dockModel: []
    property var runningList: []
    property bool startupSupported: false
    property bool launchAtStartup: false
    property bool lightTheme: false
    property string version: ""

    signal closeRequested()
    signal addRequested()
    signal resetRequested()
    signal quitRequested()
    signal unpinRequested(int index)
    signal moveRequested(int from, int to)
    signal startupToggled(bool enabled)
    signal themeToggled(bool light)
    signal launchRequested(int index)

    Theme { id: t }

    property int tab: 0

    implicitWidth: 348
    implicitHeight: body.implicitHeight

    // ---------- reusable rows
    component SectionLabel: Text {
        readonly property Theme t: Theme {}
        color: t.textDim
        font.family: t.font
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.letterSpacing: 0.2
    }

    component ToggleRow: Item {
        id: tr
        Theme { id: t }
        property string label
        property string sub
        property bool checked
        property bool enabled2: true
        signal toggled(bool checked)

        width: parent ? parent.width : 300
        implicitHeight: sub.length > 0 ? 58 : 42

        Column {
            anchors.left: parent.left
            anchors.right: sw.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            opacity: tr.enabled2 ? 1 : 0.45

            Text {
                text: tr.label
                color: t.text
                font.family: t.font
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                visible: tr.sub.length > 0
                width: parent.width
                text: tr.sub
                color: t.textDim
                font.family: t.font
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                lineHeight: 1.1
            }
        }

        Toggle {
            id: sw
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: tr.checked
            enabled2: tr.enabled2
            onToggled: (c) => tr.toggled(c)
        }
    }

    component IconButton: Item {
        id: ib
        Theme { id: t }
        property string icon
        property real glyphSize: 15
        property bool danger: false
        signal clicked()
        width: 28
        height: 28

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: ib.danger ? "#26f4212e" : t.hover
            opacity: ibHit.containsMouse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: ib.icon
            size: ib.glyphSize
            color: ib.danger && ibHit.containsMouse ? t.danger
                 : ibHit.containsMouse ? t.ink : t.textDim
        }
        MouseArea {
            id: ibHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ib.clicked()
        }
    }

    component PillButton: Item {
        id: pb
        Theme { id: t }
        property string label
        property string icon
        property bool primary: false
        signal clicked()
        implicitWidth: pbRow.implicitWidth + 32
        implicitHeight: 36

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: pb.primary ? (pbHit.containsMouse ? t.inkSoft : t.ink)
                              : (pbHit.containsMouse ? t.raised : "transparent")
            border.width: pb.primary ? 0 : 1
            border.color: t.border
            scale: pbHit.pressed ? 0.96 : 1
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 120 } }
        }
        Row {
            id: pbRow
            anchors.centerIn: parent
            spacing: 6
            Glyph {
                visible: pb.icon.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: pb.icon
                size: 15
                stroke: 2.1
                color: pb.primary ? t.inverse : t.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pb.label
                color: pb.primary ? t.inverse : t.text
                font.family: t.font
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }
        MouseArea {
            id: pbHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pb.clicked()
        }
    }

    // ---------- layout
    Column {
        id: body
        width: parent.width

        // Header
        Item {
            width: parent.width
            height: 56

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Text {
                    text: "Noch"
                    color: t.ink
                    font.family: t.font
                    font.pixelSize: 19
                    font.weight: Font.ExtraBold
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: verText.implicitWidth + 12
                    height: 20
                    radius: 10
                    color: t.raised
                    border.width: 1
                    border.color: t.borderSoft
                    Text {
                        id: verText
                        anchors.centerIn: parent
                        text: "v" + panel.version
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                icon: "close"
                onClicked: panel.closeRequested()
            }
        }

        // Tabs
        Item {
            width: parent.width
            height: 44

            Row {
                id: tabRow
                anchors.fill: parent
                Repeater {
                    model: ["General", "Apps"]
                    delegate: Item {
                        id: tabCell
                        required property int index
                        required property string modelData
                        width: tabRow.width / 2
                        height: tabRow.height
                        readonly property bool current: panel.tab === index

                        Rectangle {
                            anchors.fill: parent
                            color: t.ink
                            opacity: tabHit.containsMouse && !tabCell.current ? 0.03 : 0
                        }
                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: tabCell.modelData + (tabCell.index === 1 ? "  " + panel.dockModel.length : "")
                            color: tabCell.current ? t.text : t.textDim
                            font.family: t.font
                            font.pixelSize: 14
                            font.weight: tabCell.current ? Font.Bold : Font.Medium
                        }
                        MouseArea {
                            id: tabHit
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.tab = tabCell.index
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: t.border
            }

            Rectangle {
                id: indicator
                anchors.bottom: parent.bottom
                height: 4
                radius: 2
                width: 56
                color: t.ink
                x: panel.tab * (parent.width / 2) + (parent.width / 2 - width) / 2
                Behavior on x { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
            }
        }

        // Tab content
        Item {
            id: content
            width: parent.width
            height: panel.tab === 0 ? general.implicitHeight : apps.implicitHeight
            clip: true
            Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

            // ---- General
            Column {
                id: general
                x: 20 - (panel.tab === 0 ? 0 : 24)
                width: parent.width - 40
                topPadding: 16
                bottomPadding: 8
                spacing: 6
                opacity: panel.tab === 0 ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

                Item {
                    width: parent.width
                    height: 22
                    SectionLabel { text: "Size"; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.round(panel.prefs.sizeScale * 100) + "%"
                        color: t.text
                        font.family: t.font
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                Segmented {
                    width: parent.width
                    options: [
                        { label: "S", value: 0.8 },
                        { label: "M", value: 1.0 },
                        { label: "L", value: 1.2 },
                        { label: "XL", value: 1.4 }
                    ]
                    value: panel.prefs.sizeScale
                    onPicked: (v) => panel.prefs.sizeScale = v
                }

                SizeSlider {
                    width: parent.width
                    from: 0.7
                    to: 1.6
                    value: panel.prefs.sizeScale
                    onMoved: (v) => panel.prefs.sizeScale = Math.round(v * 100) / 100
                }

                Text {
                    width: parent.width
                    text: "Tip: drag the handle under the notch, or Ctrl + scroll on it."
                    color: t.textFaint
                    font.family: t.font
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    bottomPadding: 6
                }

                Rectangle { width: parent.width; height: 1; color: t.borderSoft }

                ToggleRow {
                    label: "Auto-hide"
                    sub: "Slides up and leaves a 5% edge. Hover the top of the screen to pull it back."
                    checked: panel.prefs.autoHide
                    onToggled: (c) => panel.prefs.autoHide = c
                }

                Item {
                    width: parent.width
                    height: 34
                    opacity: panel.prefs.autoHide ? 1 : 0.4
                    enabled: panel.prefs.autoHide
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Hide after"
                        color: t.text
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    Segmented {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 196
                        height: 30
                        options: [
                            { label: "0.3s", value: 300 },
                            { label: "0.8s", value: 800 },
                            { label: "1.5s", value: 1500 },
                            { label: "3s", value: 3000 }
                        ]
                        value: panel.prefs.hideDelay
                        onPicked: (v) => panel.prefs.hideDelay = v
                    }
                }

                Item { width: 1; height: 4 }
                Rectangle { width: parent.width; height: 1; color: t.borderSoft }

                ToggleRow {
                    label: "Black & white icons"
                    checked: panel.prefs.monoIcons
                    onToggled: (c) => panel.prefs.monoIcons = c
                }
                ToggleRow {
                    label: "Light theme"
                    sub: "Pale glass instead of black. Changes everything at once."
                    checked: panel.lightTheme
                    onToggled: (c) => panel.themeToggled(c)
                }
                ToggleRow {
                    label: "Show clock"
                    checked: panel.prefs.showClock
                    onToggled: (c) => panel.prefs.showClock = c
                }
                ToggleRow {
                    label: "System stats"
                    sub: "CPU, memory and battery beside the clock. Click them for the full monitor."
                    checked: panel.prefs.showStats
                    onToggled: (c) => panel.prefs.showStats = c
                }
                ToggleRow {
                    visible: panel.startupSupported
                    label: "Launch at startup"
                    checked: panel.launchAtStartup
                    onToggled: (c) => panel.startupToggled(c)
                }
            }

            // ---- Apps
            Column {
                id: apps
                x: 20 + (panel.tab === 1 ? 0 : 24)
                width: parent.width - 40
                topPadding: 12
                bottomPadding: 12
                spacing: 12
                opacity: panel.tab === 1 ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

                ListView {
                    id: appList
                    width: parent.width
                    height: Math.min(contentHeight, 6 * 44)
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dockModel
                    spacing: 0

                    displaced: Transition { NumberAnimation { properties: "y"; duration: 200; easing.type: Easing.OutCubic } }

                    delegate: Item {
                        id: appRow
                        required property int index
                        required property var modelData
                        width: appList.width
                        height: 44

                        readonly property bool isRunning: panel.runningList.indexOf(modelData.exe) >= 0

                        HoverHandler { id: rowHover }

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: -8
                            anchors.rightMargin: -8
                            radius: 12
                            color: t.raised
                            opacity: rowHover.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 110 } }
                        }

                        Item {
                            id: rowIcon
                            width: 24
                            height: 24
                            anchors.verticalCenter: parent.verticalCenter

                            Image {
                                id: rowImg
                                anchors.fill: parent
                                source: appRow.modelData.iconId
                                        ? "image://appicon/" + (panel.prefs.monoIcons ? t.iconMono : "color") + "/" + appRow.modelData.iconId
                                        : ""
                                sourceSize: Qt.size(128, 128)
                                mipmap: true
                                smooth: true
                                fillMode: Image.PreserveAspectFit
                                visible: status === Image.Ready
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: rowImg.status !== Image.Ready
                                radius: 7
                                color: t.raised
                                border.width: 1
                                border.color: t.border
                                Text {
                                    anchors.centerIn: parent
                                    text: (appRow.modelData.name || "?").charAt(0).toUpperCase()
                                    color: t.text
                                    font.family: t.font
                                    font.pixelSize: 12
                                    font.weight: Font.Bold
                                }
                            }
                        }

                        Column {
                            anchors.left: rowIcon.right
                            anchors.leftMargin: 12
                            anchors.right: rowActions.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Row {
                                spacing: 6
                                Text {
                                    text: appRow.modelData.name
                                    color: t.text
                                    font.family: t.font
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }
                                Rectangle {
                                    visible: appRow.isRunning
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: t.ink
                                }
                            }
                            Text {
                                width: parent.width
                                text: appRow.modelData.exists === false && appRow.modelData.path
                                      ? "Missing - " + appRow.modelData.path
                                      : (appRow.modelData.path || "Not installed")
                                color: appRow.modelData.exists === false && appRow.modelData.path ? t.danger : t.textDim
                                elide: Text.ElideMiddle
                                font.family: t.font
                                font.pixelSize: 11
                            }
                        }

                        Row {
                            id: rowActions
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0
                            opacity: rowHover.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 110 } }

                            IconButton {
                                icon: "left"
                                visible: appRow.index > 0
                                onClicked: panel.moveRequested(appRow.index, appRow.index - 1)
                            }
                            IconButton {
                                icon: "right"
                                visible: appRow.index < panel.dockModel.length - 1
                                onClicked: panel.moveRequested(appRow.index, appRow.index + 1)
                            }
                            IconButton {
                                icon: "close"
                                danger: true
                                onClicked: panel.unpinRequested(appRow.index)
                            }
                        }
                    }
                }

                Text {
                    visible: panel.dockModel.length === 0
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 18
                    bottomPadding: 6
                    text: "No apps pinned yet."
                    color: t.textDim
                    font.family: t.font
                    font.pixelSize: 13
                }

                Row {
                    spacing: 8
                    PillButton {
                        primary: true
                        icon: "plus"
                        label: "Add app"
                        onClicked: panel.addRequested()
                    }
                    PillButton {
                        icon: "reset"
                        label: "Auto-detect"
                        onClicked: panel.resetRequested()
                    }
                }

                Text {
                    width: parent.width
                    text: "Or drop any app, shortcut or folder onto the notch. Right-click an icon for more."
                    color: t.textFaint
                    font.family: t.font
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: t.border }

        Item {
            width: parent.width
            height: 50
            MenuRow {
                anchors.fill: parent
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                label: "Quit Noch"
                icon: "power"
                danger: true
                onTriggered: panel.quitRequested()
            }
        }
    }
}
