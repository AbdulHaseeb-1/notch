import QtQuick

// Settings flyout under the notch. Unified header with a segmented switcher
// (General / Apps), grouped setting cards on the General tab, and a dedicated
// "Pinned in Notch" + searchable "Add Apps" list on the Apps tab.
Item {
    id: panel

    property var prefs
    property var dockModel: []
    property var runningList: []
    property bool startupSupported: false
    property bool launchAtStartup: false
    property bool lightTheme: false
    property bool blurSupported: false
    property bool blurBehind: false
    // Tallest the tab content may grow before it scrolls (so the card always fits the screen).
    property real maxBodyHeight: 100000
    property string version: ""

    signal closeRequested()
    signal addRequested()
    signal pinAppRequested(string name, string path)
    signal resetRequested()
    signal quitRequested()
    signal unpinRequested(int index)
    signal moveRequested(int from, int to)
    signal startupToggled(bool enabled)
    signal themeToggled(bool light)
    signal blurToggled(bool on)

    Theme { id: t }

    property int tab: 0
    property string appFilter: ""
    property var availableList: []

    readonly property real headerH: 58
    readonly property real footerH: 46

    implicitWidth: 388
    implicitHeight: headerH + 1 + content.height + 1 + footerH
    // Where the card is heading, for the click-through mask (the animated
    // height would rebuild the window mask on every frame).
    readonly property real naturalBodyHeight: tab === 0 ? general.implicitHeight : appsCol.implicitHeight
    readonly property real targetBodyHeight: Math.min(naturalBodyHeight, maxBodyHeight)
    readonly property real targetHeight: headerH + 1 + targetBodyHeight + 1 + footerH

    function refreshAvailableApps() {
        const raw = search.availableApps(panel.appFilter)
        const filtered = []
        for (let i = 0; i < raw.length; ++i) {
            const item = raw[i]
            if (!dock.isPinned(item.path || "", item.exe || ""))
                filtered.push(item)
        }
        panel.availableList = filtered
    }

    onDockModelChanged: refreshAvailableApps()
    onRunningListChanged: if (panel.visible) refreshAvailableApps()
    onAppFilterChanged: refreshAvailableApps()
    onTabChanged: if (tab === 1) refreshAvailableApps()
    Component.onCompleted: refreshAvailableApps()

    // The Start Menu index lands asynchronously now; refresh when it does.
    Connections {
        target: search
        function onIndexReady() {
            if (panel.visible)
                panel.refreshAvailableApps()
        }
    }

    // ---------- reusable components
    component SectionHeader: Item {
        property string title
        property string rightText: ""
        width: parent ? parent.width : 300
        height: 20

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: parent.title.toUpperCase()
            color: t.textDim
            font.family: t.font
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }
        Text {
            visible: parent.rightText.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: parent.rightText
            color: t.textFaint
            font.family: t.font
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    component GroupCard: Rectangle {
        default property alias cardContent: innerCol.data
        width: parent ? parent.width : 340
        implicitHeight: innerCol.implicitHeight + 20
        radius: 14
        color: t.raised
        border.width: 1
        border.color: t.borderSoft

        Column {
            id: innerCol
            x: 14
            y: 10
            width: parent.width - 28
            spacing: 8
        }
    }

    component CardDivider: Rectangle {
        width: parent ? parent.width : 300
        height: 1
        color: t.borderSoft
    }

    component ToggleRow: Item {
        id: tr
        property string label
        property string sub
        property bool checked
        property bool enabled2: true
        signal toggled(bool checked)

        width: parent ? parent.width : 300
        implicitHeight: Math.max(36, textCol.implicitHeight + 6)

        Column {
            id: textCol
            anchors.left: parent.left
            anchors.right: sw.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            opacity: tr.enabled2 ? 1 : 0.45

            Text {
                text: tr.label
                color: t.text
                font.family: t.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                visible: tr.sub.length > 0
                width: parent.width
                text: tr.sub
                color: t.textDim
                font.family: t.font
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeight: 1.12
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
        property string icon
        property real glyphSize: 14
        property bool danger: false
        signal clicked()
        width: 26
        height: 26

        Glyph {
            anchors.centerIn: parent
            name: ib.icon
            size: ib.glyphSize
            color: ib.danger && ibHit.containsMouse ? t.danger
                 : ibHit.containsMouse ? t.ink : t.textDim
            scale: ibHit.pressed ? 0.86 : 1
            Behavior on scale { NumberAnimation { duration: 100 } }
        }
        MouseArea {
            id: ibHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ib.clicked()
        }
    }

    component SmallPillButton: Item {
        id: spb
        property string label
        property string icon: ""
        property bool primary: false
        property bool danger: false
        signal clicked()
        implicitWidth: spbRow.implicitWidth + 20
        implicitHeight: 28

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: spb.primary ? t.ink
                 : spb.danger ? "transparent"
                 : t.raised
            border.width: spb.primary ? 0 : 1
            border.color: spb.danger && spbHit.containsMouse ? t.danger : t.border
            scale: spbHit.pressed ? 0.95 : 1
            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
        Row {
            id: spbRow
            anchors.centerIn: parent
            spacing: 5
            Glyph {
                visible: spb.icon.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: spb.icon
                size: 12
                stroke: 2.1
                color: spb.primary ? t.inverse : (spb.danger ? t.danger : t.text)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: spb.label
                color: spb.primary ? t.inverse : (spb.danger ? t.danger : t.text)
                font.family: t.font
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }
        MouseArea {
            id: spbHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: spb.clicked()
        }
    }

    // ---------- header (matches HubPanel structure)
    Item {
        id: header
        width: parent.width
        height: panel.headerH

        Segmented {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: closeBtn.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 34
            options: [
                { label: qsTr("General"), value: 0 },
                { label: qsTr("Apps (%1)").arg(panel.dockModel.length), value: 1 }
            ]
            value: panel.tab
            onPicked: (v) => panel.tab = v
        }

        IconButton {
            id: closeBtn
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28
            icon: "close"
            glyphSize: 15
            onClicked: panel.closeRequested()
        }
    }

    Rectangle {
        id: topRule
        anchors.top: header.bottom
        width: parent.width
        height: 1
        color: t.borderSoft
    }

    // ---------- body
    Item {
        id: content
        anchors.top: topRule.bottom
        width: parent.width
        height: panel.targetBodyHeight
        clip: true
        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // ---- Tab 0: General
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: general.implicitHeight
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 3500
            clip: true
            visible: panel.tab === 0 || general.opacity > 0.01
            // The active tab fades in above the one fading out; without this
            // the apps tab stayed drawn on top during the fade back.
            z: panel.tab === 0 ? 1 : 0

            Column {
                id: general
                x: 18 - (panel.tab === 0 ? 0 : 18)
                width: content.width - 36
                topPadding: 14
                bottomPadding: 14
                spacing: 10
                opacity: panel.tab === 0 ? 1 : 0
                enabled: panel.tab === 0
                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                SectionHeader {
                    title: qsTr("Size & Scale")
                    rightText: Math.round(panel.prefs.sizeScale * 100) + "%"
                }

                GroupCard {
                    Segmented {
                        width: parent.width
                        height: 30
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
                        text: qsTr("Drag the handle below the notch or Ctrl + scroll on it.")
                        color: t.textFaint
                        font.family: t.font
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                SectionHeader { title: qsTr("Appearance & Bar") }

                GroupCard {
                    ToggleRow {
                        label: qsTr("Light theme")
                        sub: qsTr("Pale frosted glass instead of dark glass.")
                        checked: panel.lightTheme
                        onToggled: (c) => panel.themeToggled(c)
                    }
                    CardDivider {}
                    ToggleRow {
                        label: qsTr("Monochrome icons")
                        sub: qsTr("Render dock icons in high-contrast monochrome.")
                        checked: panel.prefs.monoIcons
                        onToggled: (c) => panel.prefs.monoIcons = c
                    }
                    CardDivider { visible: panel.blurSupported }
                    ToggleRow {
                        visible: panel.blurSupported
                        label: qsTr("Blur behind (beta)")
                        sub: qsTr("Real desktop blur behind the glass surface.")
                        checked: panel.blurBehind
                        onToggled: (c) => panel.blurToggled(c)
                    }
                    CardDivider {}
                    ToggleRow {
                        label: qsTr("Show clock")
                        checked: panel.prefs.showClock
                        onToggled: (c) => panel.prefs.showClock = c
                    }
                    CardDivider {}
                    ToggleRow {
                        label: qsTr("System stats")
                        sub: qsTr("Show live CPU, RAM and battery next to the clock.")
                        checked: panel.prefs.showStats
                        onToggled: (c) => panel.prefs.showStats = c
                    }
                }

                SectionHeader { title: qsTr("Behavior & System") }

                GroupCard {
                    ToggleRow {
                        label: qsTr("Auto-hide notch")
                        sub: qsTr("Tucks into the top edge; hover the top of the screen to reveal.")
                        checked: panel.prefs.autoHide
                        onToggled: (c) => panel.prefs.autoHide = c
                    }

                    Item {
                        visible: panel.prefs.autoHide
                        width: parent.width
                        height: 30
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Hide delay")
                            color: t.textDim
                            font.family: t.font
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        Segmented {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 232
                            height: 28
                            options: [
                                { label: "0.3s", value: 300 },
                                { label: "0.8s", value: 800 },
                                { label: "1.2s", value: 1200 },
                                { label: "1.5s", value: 1500 },
                                { label: "3s", value: 3000 }
                            ]
                            value: panel.prefs.hideDelay
                            onPicked: (v) => panel.prefs.hideDelay = v
                        }
                    }

                    CardDivider {}
                    ToggleRow {
                        label: qsTr("Quiet in fullscreen")
                        sub: qsTr("Ignore top-edge hover while a game or fullscreen video is active.")
                        checked: panel.prefs.suppressInFullscreen
                        onToggled: (c) => panel.prefs.suppressInFullscreen = c
                    }
                    CardDivider {}
                    ToggleRow {
                        label: qsTr("Desktop buddy")
                        sub: qsTr("A little helper on a rope at the top edge. Click it for plans, demo charts and research notes.")
                        checked: panel.prefs.buddyEnabled
                        onToggled: (c) => panel.prefs.buddyEnabled = c
                    }
                    CardDivider {}
                    ToggleRow {
                        label: qsTr("Command palette hotkey")
                        sub: !panel.prefs.hotkeyPalette
                             ? qsTr("Off. Ctrl + Alt + Space summons search from anywhere.")
                             : (!hotkey.registered
                                ? qsTr("Shortcut unavailable on this PC.")
                                : (hotkey.preferred
                                   ? qsTr("Press Ctrl + Alt + Space anywhere to search.")
                                   : qsTr("Active on %1.").arg(hotkey.shortcut)))
                        checked: panel.prefs.hotkeyPalette
                        onToggled: (c) => panel.prefs.hotkeyPalette = c
                    }
                    CardDivider { visible: panel.startupSupported }
                    ToggleRow {
                        visible: panel.startupSupported
                        label: qsTr("Launch at startup")
                        checked: panel.launchAtStartup
                        onToggled: (c) => panel.startupToggled(c)
                    }
                }
            }
        }

        // ---- Tab 1: Apps (Pinned in Notch + List of Installed Apps to Add)
        Flickable {
            anchors.fill: parent
            contentWidth: width
            contentHeight: appsCol.implicitHeight
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            visible: panel.tab === 1 || appsCol.opacity > 0.01
            z: panel.tab === 1 ? 1 : 0

            Column {
                id: appsCol
                x: 18 + (panel.tab === 1 ? 0 : 18)
                width: content.width - 36
                topPadding: 14
                bottomPadding: 14
                spacing: 10
                opacity: panel.tab === 1 ? 1 : 0
                enabled: panel.tab === 1
                Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                // ── Section 1: Pinned in Notch ──────────────────────────────
                Item {
                    width: parent.width
                    height: 26

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("PINNED IN NOTCH (%1)").arg(panel.dockModel.length)
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.8
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        SmallPillButton {
                            icon: "reset"
                            label: qsTr("Defaults")
                            onClicked: panel.resetRequested()
                        }
                        SmallPillButton {
                            icon: "folder"
                            label: qsTr("Browse…")
                            onClicked: panel.addRequested()
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: panel.dockModel.length === 0 ? 52 : Math.min(pinnedList.contentHeight, 4 * 40) + 8
                    radius: 14
                    color: t.raised
                    border.width: 1
                    border.color: t.borderSoft
                    clip: true
                    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                    Text {
                        visible: panel.dockModel.length === 0
                        anchors.centerIn: parent
                        text: qsTr("No apps pinned — click + Add below to pin apps")
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 12
                    }

                    ListView {
                        id: pinnedList
                        visible: panel.dockModel.length > 0
                        x: 6
                        y: 4
                        width: parent.width - 12
                        height: parent.height - 8
                        clip: true
                        interactive: contentHeight > height
                        boundsBehavior: Flickable.StopAtBounds
                        model: panel.dockModel
                        spacing: 0

                        displaced: Transition {
                            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
                        }

                        delegate: Item {
                            id: appRow
                            required property int index
                            required property var modelData
                            width: pinnedList.width
                            height: 40

                            readonly property bool isRunning: panel.runningList.indexOf(modelData.exe) >= 0

                            HoverHandler { id: rowHover }

                            Item {
                                id: rowIcon
                                x: 10
                                width: 22
                                height: 22
                                anchors.verticalCenter: parent.verticalCenter

                                Image {
                                    id: rowImg
                                    anchors.fill: parent
                                    source: appRow.modelData.iconId
                                            ? "image://appicon/" + (panel.prefs.monoIcons ? t.iconMono : "color") + "/" + appRow.modelData.iconId
                                            : ""
                                    sourceSize: Qt.size(96, 96)
                                    mipmap: true
                                    smooth: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: status === Image.Ready
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    visible: rowImg.status !== Image.Ready
                                    radius: 6
                                    color: t.raised
                                    border.width: 1
                                    border.color: t.border
                                    Text {
                                        anchors.centerIn: parent
                                        text: (appRow.modelData.name || "?").charAt(0).toUpperCase()
                                        color: t.text
                                        font.family: t.font
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            Row {
                                anchors.left: rowIcon.right
                                anchors.leftMargin: 10
                                anchors.right: rowActions.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6

                                Text {
                                    text: appRow.modelData.name
                                    color: t.text
                                    elide: Text.ElideRight
                                    font.family: t.font
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Rectangle {
                                    visible: appRow.isRunning
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: t.success
                                }
                            }

                            Row {
                                id: rowActions
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                opacity: rowHover.hovered ? 1 : 0.55
                                Behavior on opacity { NumberAnimation { duration: 110 } }

                                IconButton {
                                    icon: "up"
                                    glyphSize: 13
                                    visible: appRow.index > 0
                                    onClicked: panel.moveRequested(appRow.index, appRow.index - 1)
                                }
                                IconButton {
                                    icon: "down"
                                    glyphSize: 13
                                    visible: appRow.index < panel.dockModel.length - 1
                                    onClicked: panel.moveRequested(appRow.index, appRow.index + 1)
                                }
                                IconButton {
                                    icon: "close"
                                    glyphSize: 13
                                    danger: true
                                    onClicked: panel.unpinRequested(appRow.index)
                                }
                            }
                        }
                    }
                }

                // ── Section 2: Installed Apps List to Add ───────────────────
                SectionHeader {
                    title: qsTr("Add Apps")
                    rightText: qsTr("%1 available").arg(panel.availableList.length)
                }

                // Search / Filter bar for installed apps
                Rectangle {
                    width: parent.width
                    height: 36
                    radius: 11
                    color: t.raised
                    border.width: 1
                    border.color: filterInput.activeFocus ? t.borderStrong : t.borderSoft
                    Behavior on border.color { ColorAnimation { duration: 130 } }

                    Glyph {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: 14
                        stroke: 2
                        color: filterInput.activeFocus ? t.ink : t.textDim
                    }

                    TextInput {
                        id: filterInput
                        anchors.left: parent.left
                        anchors.leftMargin: 34
                        anchors.right: clearFilterBtn.visible ? clearFilterBtn.left : parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: t.ink
                        selectionColor: t.ink
                        selectedTextColor: t.inverse
                        font.family: t.font
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        clip: true
                        selectByMouse: true
                        onTextChanged: panel.appFilter = text

                        Text {
                            anchors.fill: parent
                            visible: filterInput.text.length === 0
                            verticalAlignment: TextInput.AlignVCenter
                            text: qsTr("Search installed apps to add…")
                            color: t.textFaint
                            font: filterInput.font
                        }
                    }

                    IconButton {
                        id: clearFilterBtn
                        visible: filterInput.text.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24
                        icon: "close"
                        glyphSize: 11
                        onClicked: {
                            filterInput.text = ""
                            filterInput.forceActiveFocus()
                        }
                    }
                }

                // Scrollable list of installed apps ready to add
                Rectangle {
                    width: parent.width
                    height: panel.availableList.length === 0 ? 64 : Math.min(addList.contentHeight + 8, 5 * 44 + 8)
                    radius: 14
                    color: t.raised
                    border.width: 1
                    border.color: t.borderSoft
                    clip: true
                    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                    Text {
                        visible: panel.availableList.length === 0
                        anchors.centerIn: parent
                        text: filterInput.text.length > 0
                              ? qsTr("No matching unpinned apps found")
                              : qsTr("All detected apps are already pinned")
                        color: t.textDim
                        font.family: t.font
                        font.pixelSize: 12
                    }

                    ListView {
                        id: addList
                        visible: panel.availableList.length > 0
                        x: 6
                        y: 4
                        width: parent.width - 12
                        height: parent.height - 8
                        clip: true
                        interactive: contentHeight > height
                        boundsBehavior: Flickable.StopAtBounds
                        model: panel.availableList
                        spacing: 2

                        displaced: Transition {
                            NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
                        }

                        delegate: Item {
                            id: candRow
                            required property int index
                            required property var modelData
                            width: addList.width
                            height: 42

                            HoverHandler { id: candHover }

                            Item {
                                id: candIcon
                                x: 10
                                width: 24
                                height: 24
                                anchors.verticalCenter: parent.verticalCenter

                                Image {
                                    id: candImg
                                    anchors.fill: parent
                                    source: candRow.modelData.iconId
                                            ? "image://appicon/color/" + candRow.modelData.iconId
                                            : ""
                                    sourceSize: Qt.size(96, 96)
                                    mipmap: true
                                    smooth: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: status === Image.Ready
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    visible: candImg.status !== Image.Ready
                                    radius: 7
                                    color: t.raised
                                    border.width: 1
                                    border.color: t.border
                                    Text {
                                        anchors.centerIn: parent
                                        text: (candRow.modelData.name || "?").charAt(0).toUpperCase()
                                        color: t.text
                                        font.family: t.font
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            Text {
                                anchors.left: candIcon.right
                                anchors.leftMargin: 10
                                anchors.right: addPill.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: candRow.modelData.name
                                color: t.text
                                elide: Text.ElideRight
                                font.family: t.font
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            Rectangle {
                                id: addPill
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: addPillRow.implicitWidth + 16
                                height: 26
                                radius: 13
                                color: t.tile
                                border.width: 1
                                border.color: candHover.hovered ? t.borderStrong : t.border
                                scale: candTap.pressed ? 0.94 : 1
                                Behavior on scale { NumberAnimation { duration: 100 } }

                                Row {
                                    id: addPillRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Glyph {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "plus"
                                        size: 11
                                        stroke: 2.2
                                        color: t.ink
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Add")
                                        color: t.ink
                                        font.family: t.font
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            TapHandler {
                                id: candTap
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: panel.pinAppRequested(candRow.modelData.name, candRow.modelData.path)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: bottomRule
        anchors.top: content.bottom
        width: parent.width
        height: 1
        color: t.borderSoft
    }

    // ---------- footer
    Item {
        id: footer
        anchors.top: bottomRule.bottom
        width: parent.width
        height: panel.footerH

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7

            Text {
                text: "Noch"
                color: t.text
                font.family: t.font
                font.pixelSize: 13
                font.weight: Font.ExtraBold
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: verText.implicitWidth + 10
                height: 18
                radius: 9
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

        SmallPillButton {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            icon: "power"
            label: qsTr("Quit Noch")
            danger: true
            onClicked: panel.quitRequested()
        }
    }
}
