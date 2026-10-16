import QtQuick

// The command palette: Ctrl+Alt+Space summons the notch and this card. One
// field, one ranked list: installed apps, open windows, the inline calculator,
// built-in commands and a web search fallback. The C++ side (AppSearch) does
// the finding; this does the looking and the keys.
Item {
    id: palette

    // Raised when the palette should close because the user is done with it
    // (Escape, or a row was run).
    signal dismissed()

    Theme { id: t }

    property var results: []
    property int currentIndex: 0
    readonly property int rowH: 44
    readonly property int maxRows: 7

    implicitWidth: 520
    readonly property real targetListHeight: Math.min(results.length, palette.maxRows) * palette.rowH
    readonly property real targetHeight: field.implicitHeight + 1 + targetListHeight + 1 + footer.implicitHeight
    implicitHeight: field.implicitHeight + 1 + list.height + 1 + footer.implicitHeight

    function reset() {
        if (paletteInput.text === "")
            refresh()
        else
            paletteInput.text = "" // refresh() runs from onTextChanged
    }

    function refresh() {
        results = search.search(paletteInput.text)
        currentIndex = 0
        list.positionViewAtBeginning()
    }

    function move(delta) {
        if (results.length === 0)
            return
        currentIndex = (currentIndex + delta + results.length) % results.length
        list.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    function run(item) {
        if (!item)
            return
        dismissed()
        search.activate(item)
    }

    function runCurrent() {
        run(results[currentIndex])
    }

    function takeFocus() {
        refresh()
        paletteInput.forceActiveFocus()
        paletteInput.selectAll()
    }

    // The Start Menu scan now runs off the GUI thread; refresh once it lands
    // so the very first keystroke fills up with real results when they arrive.
    Connections {
        target: search
        function onIndexReady() { palette.refresh() }
    }

    Column {
        anchors.fill: parent

        // ── Search field ────────────────────────────────────────────────
        Item {
            id: field
            width: parent.width
            implicitHeight: 54

            Glyph {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 17
                stroke: 2
                color: paletteInput.activeFocus ? t.ink : t.textFaint
            }

            TextInput {
                id: paletteInput
                anchors.left: parent.left
                anchors.leftMargin: 46
                anchors.right: clearBtn.visible ? clearBtn.left : parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                color: t.ink
                selectionColor: t.ink
                selectedTextColor: t.inverse
                font.family: t.font
                font.pixelSize: 15
                font.weight: Font.Medium
                clip: true
                selectByMouse: true

                onTextChanged: palette.refresh()
                onAccepted: palette.runCurrent()

                Keys.onDownPressed: (event) => {
                    palette.move(1)
                    event.accepted = true
                }
                Keys.onUpPressed: (event) => {
                    palette.move(-1)
                    event.accepted = true
                }
                Keys.onEscapePressed: (event) => {
                    palette.dismissed()
                    event.accepted = true
                }

                Text {
                    anchors.fill: parent
                    visible: paletteInput.text.length === 0
                    verticalAlignment: TextInput.AlignVCenter
                    text: qsTr("Search apps, switch windows, calculate, or search the web…")
                    color: t.textFaint
                    font: paletteInput.font
                }
            }

            Item {
                id: clearBtn
                visible: paletteInput.text.length > 0
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24

                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 12
                    stroke: 2
                    color: clearHit.containsMouse ? t.ink : t.textDim
                }
                MouseArea {
                    id: clearHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        paletteInput.text = ""
                        paletteInput.forceActiveFocus()
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: t.borderSoft
        }

        // ── Results ─────────────────────────────────────────────────────
        ListView {
            id: list
            width: parent.width
            height: palette.targetListHeight
            Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            interactive: count * palette.rowH > height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            model: palette.results
            onCountChanged: palette.currentIndex = Math.min(palette.currentIndex, Math.max(0, count - 1))

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: list.width
                height: palette.rowH
                readonly property bool active: index === palette.currentIndex

                Rectangle {
                    x: 8
                    y: 2
                    width: parent.width - 16
                    height: parent.height - 4
                    radius: 10
                    color: row.active ? t.tileActive : "transparent"
                    border.width: row.active ? 1 : 0
                    border.color: t.tileEdge
                }

                HoverHandler {
                    id: rowHover
                    onHoveredChanged: if (hovered) palette.currentIndex = row.index
                }

                // Leading icon: a real app icon when there is one, a glyph
                // otherwise (commands, maths, the web).
                Item {
                    id: iconBox
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26

                    Image {
                        id: rowIcon
                        anchors.fill: parent
                        source: row.modelData.iconId && row.modelData.kind !== "command"
                                ? "image://appicon/color/" + row.modelData.iconId
                                : ""
                        sourceSize: Qt.size(96, 96)
                        mipmap: true
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }
                    Glyph {
                        anchors.centerIn: parent
                        visible: !rowIcon.visible
                        name: row.modelData.icon || "plus"
                        size: 17
                        stroke: 1.9
                        color: t.text
                    }
                }

                Text {
                    id: rowTitle
                    anchors.left: iconBox.right
                    anchors.leftMargin: 14
                    anchors.right: rowSub.left
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.title || ""
                    color: t.ink
                    elide: Text.ElideRight
                    font.family: t.font
                    font.pixelSize: 14
                    font.weight: row.modelData.kind === "calc" ? Font.Bold : Font.DemiBold

                    // The calculator row shows the sum being evaluated.
                    Text {
                        readonly property bool fits: rowTitle.text.length < 26
                        visible: row.modelData.kind === "calc" && fits
                        x: parent.contentWidth + 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.subtitle || ""
                        color: t.textFaint
                        font.family: t.font
                        font.pixelSize: 12
                    }
                }

                Text {
                    id: rowSub
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, row.width * 0.42)
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.kind === "calc" ? "" : (row.modelData.subtitle || "")
                    color: row.active ? t.textDim : t.textFaint
                    elide: Text.ElideRight
                    font.family: t.font
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: palette.run(row.modelData)
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: t.borderSoft
        }

        // ── Footer hints ────────────────────────────────────────────────
        Item {
            id: footer
            width: parent.width
            implicitHeight: 34

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                component Hint: Row {
                    Theme { id: ht }
                    property string key
                    property string what
                    spacing: 5
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(18, hintKey.implicitWidth + 8)
                        height: 17
                        radius: 5
                        color: ht.raised
                        border.width: 1
                        border.color: ht.border
                        Text {
                            id: hintKey
                            anchors.centerIn: parent
                            text: parent.parent.key
                            color: ht.textDim
                            font.family: ht.font
                            font.pixelSize: 10
                            font.weight: Font.Bold
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.what
                        color: ht.textFaint
                        font.family: ht.font
                        font.pixelSize: 10
                        font.weight: Font.Medium
                    }
                }

                Hint { key: "↑↓"; what: qsTr("navigate") }
                Hint { key: "↵"; what: qsTr("run") }
                Hint { key: "esc"; what: qsTr("close") }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                visible: palette.results.length > 0
                text: qsTr("%1 result(s)").arg(palette.results.length)
                color: t.textFaint
                font.family: t.font
                font.pixelSize: 10
            }
        }
    }

    Component.onCompleted: reset()
}
