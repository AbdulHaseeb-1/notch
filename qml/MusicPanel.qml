import QtQuick
import QtQuick.Shapes

// "NOW PLAYING" card (design reference): near-black card,
// framed artwork, title / artist with heart + volume, thin purple scrubber,
// and a transport row spread edge to edge around a purple play button.
Item {
    id: panel

    signal closeRequested()
    signal addRequested()
    signal toastRequested(string text)

    Theme { id: t }

    // ---- palette
    readonly property color cardBg:      "#0a0a0d"
    readonly property color bezelBg:     "#17122a"
    readonly property color bezelBorder: "#2a2145"
    readonly property color accent:      "#a855f7"
    readonly property color accentLight: "#c084fc"
    readonly property color accentDeep:  "#7c3aed"
    readonly property color textMuted:   "#8b8f98"
    readonly property color textFaint:   "#5b5f68"

    readonly property real pad: 22
    readonly property real cardRadius: 24

    property bool queueOpen: false
    property bool menuOpen: false
    property var liked: ({})
    readonly property bool isLiked: player.hasTrack && !!liked[player.title]

    function toggleLike() {
        if (!player.hasTrack)
            return
        const next = Object.assign({}, liked)
        if (next[player.title])
            delete next[player.title]
        else
            next[player.title] = true
        liked = next
    }

    function fmtClock(ms) {
        ms = Math.max(0, Math.floor(ms || 0))
        const total = Math.floor(ms / 1000)
        const s = total % 60
        const m = Math.floor(total / 60) % 60
        const h = Math.floor(total / 3600)
        const pad2 = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad2(m) + ":" + pad2(s) : m + ":" + pad2(s)
    }

    function noteAdded(n) {
        if (n > 0)
            panel.toastRequested(n === 1 ? qsTr("Added to player") : qsTr("Added %1 songs").arg(n))
    }

    readonly property bool playing: player.state === 1
    readonly property string displayTitle: player.hasTrack ? player.title : qsTr("Nothing playing")
    readonly property string displayArtist: player.hasTrack
        ? (player.artist || qsTr("Unknown artist"))
        : qsTr("Add songs or drop audio files here")
    readonly property real displayDuration: player.hasTrack ? player.duration : 0
    readonly property real displayPosition: seek.dragging ? seek.dragRatio * displayDuration : player.position
    readonly property real progressRatio: displayDuration > 0
        ? (seek.dragging ? seek.dragRatio : Math.max(0, Math.min(1, player.position / displayDuration)))
        : 0

    implicitWidth: 380
    implicitHeight: body.implicitHeight

    // ---------------------------------------------------------------- card
    Glass {
        id: card
        anchors.fill: parent
        radius: panel.cardRadius
        tint: panel.cardBg
        edge: "#1e1a2b"
        clip: true

    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            panel.noteAdded(player.addUrls(drop.urls))
            drop.accept(Qt.CopyAction)
        }
    }

    component IconHit: Item {
        id: ih
        property string icon
        property real glyphSize: 16
        property color color: "#8b8f98"
        property color hoverColor: "#ffffff"
        property bool filled: false
        property real stroke: 1.8
        readonly property bool hovered: ihHit.containsMouse
        signal clicked()
        width: 32
        height: 32

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#ffffff"
            opacity: ihHit.pressed ? 0.12 : (ihHit.containsMouse ? 0.06 : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: ih.icon
            size: ih.glyphSize
            stroke: ih.stroke
            filled: ih.filled
            color: ihHit.containsMouse ? ih.hoverColor : ih.color
            scale: ihHit.pressed ? 0.86 : 1
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 110 } }
        }
        MouseArea {
            id: ihHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ih.clicked()
        }
    }

    Column {
        id: body
        width: parent.width

        // â”€â”€ Header: âˆ¨   NOW PLAYING   Â·Â·Â· â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            width: parent.width
            height: 50

            IconHit {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "chevron-down"
                glyphSize: 16
                stroke: 2
                onClicked: panel.closeRequested()
            }

            Text {
                anchors.centerIn: parent
                text: qsTr("NOW PLAYING")
                color: panel.textMuted
                font.family: t.font
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 2.4
            }

            IconHit {
                id: moreBtn
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "more"
                glyphSize: 16
                color: panel.menuOpen ? "#ffffff" : panel.textMuted
                onClicked: panel.menuOpen = !panel.menuOpen
            }
        }

        // â”€â”€ Artwork â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            width: parent.width
            height: art.height + 4

            Item {
                id: art
                anchors.horizontalCenter: parent.horizontalCenter
                width: 196
                height: 196

                Rectangle {
                    id: bezel
                    anchors.fill: parent
                    radius: 18
                    color: panel.bezelBg
                    border.width: 1
                    border.color: panel.bezelBorder

                    Rectangle {
                        id: artInner
                        anchors.centerIn: parent
                        width: parent.width - 38
                        height: width
                        radius: 8
                        color: "#0b0816"
                        clip: true

                        Image {
                            id: coverImg
                            anchors.fill: parent
                            source: player.hasCover ? "image://cover/" + player.coverRev : ""
                            sourceSize: Qt.size(320, 320)
                            fillMode: Image.PreserveAspectCrop
                            visible: player.hasCover && status === Image.Ready
                            asynchronous: true
                            smooth: true
                            mipmap: true
                        }

                        // Default artwork: glowing polyhedron (shown when the file
                        // carries no cover).
                        Item {
                            id: defaultArt
                            anchors.fill: parent
                            visible: !coverImg.visible

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#1c0f33" }
                                    GradientStop { position: 1.0; color: "#08050f" }
                                }
                            }

                            Shape {
                                anchors.fill: parent
                                ShapePath {
                                    strokeWidth: -1
                                    strokeColor: "transparent"
                                    fillGradient: RadialGradient {
                                        centerX: defaultArt.width / 2
                                        centerY: defaultArt.height / 2
                                        centerRadius: defaultArt.width * 0.5
                                        focalX: centerX
                                        focalY: centerY
                                        GradientStop { position: 0.0; color: "#998b5cf6" }
                                        GradientStop { position: 0.5; color: "#265b21b6" }
                                        GradientStop { position: 1.0; color: "#005b21b6" }
                                    }
                                    startX: 0; startY: 0
                                    PathLine { x: defaultArt.width; y: 0 }
                                    PathLine { x: defaultArt.width; y: defaultArt.height }
                                    PathLine { x: 0; y: defaultArt.height }
                                    PathLine { x: 0; y: 0 }
                                }
                            }

                            // d20-style wireframe on a 100-unit grid.
                            Item {
                                id: gem
                                anchors.centerIn: parent
                                width: parent.width * 0.56
                                height: width
                                property real bob: 0
                                anchors.verticalCenterOffset: -3 * bob

                                SequentialAnimation on bob {
                                    running: panel.playing && defaultArt.visible
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 0; to: 1; duration: 1600; easing.type: Easing.InOutSine }
                                    NumberAnimation { from: 1; to: 0; duration: 1600; easing.type: Easing.InOutSine }
                                }

                                Shape {
                                    anchors.fill: parent
                                    preferredRendererType: Shape.CurveRenderer
                                    transform: Scale { xScale: gem.width / 100; yScale: gem.height / 100 }

                                    // faces
                                    ShapePath {
                                        strokeWidth: -1
                                        strokeColor: "transparent"
                                        fillGradient: LinearGradient {
                                            x1: 20; y1: 10; x2: 80; y2: 90
                                            GradientStop { position: 0.0; color: "#80c084fc" }
                                            GradientStop { position: 1.0; color: "#304c1d95" }
                                        }
                                        PathSvg { path: "M50 10 L85 30 L85 70 L50 90 L15 70 L15 30 Z" }
                                    }
                                    ShapePath {
                                        strokeWidth: -1
                                        strokeColor: "transparent"
                                        fillColor: "#55e9d5ff"
                                        PathSvg { path: "M50 32 L70 66 L30 66 Z" }
                                    }
                                    // edges
                                    ShapePath {
                                        strokeColor: "#e9d5ff"
                                        strokeWidth: 1.6
                                        fillColor: "transparent"
                                        joinStyle: ShapePath.RoundJoin
                                        capStyle: ShapePath.RoundCap
                                        PathSvg {
                                            path: "M50 10 L85 30 L85 70 L50 90 L15 70 L15 30 Z M50 32 L70 66 L30 66 Z M50 10 L50 32 M15 30 L50 32 M85 30 L50 32 M85 30 L70 66 M85 70 L70 66 M50 90 L70 66 M50 90 L30 66 M15 70 L30 66 M15 30 L30 66"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item { width: 1; height: 24 }

        // â”€â”€ Title / artist + heart & volume â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            width: parent.width
            height: 40

            Column {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.right: metaActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: panel.displayTitle
                    color: "#ffffff"
                    elide: Text.ElideRight
                    font.family: t.font
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: panel.displayArtist
                    color: panel.textMuted
                    elide: Text.ElideRight
                    font.family: t.font
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            Row {
                id: metaActions
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                IconHit {
                    icon: "heart"
                    glyphSize: 17
                    filled: panel.isLiked
                    color: panel.accent
                    hoverColor: panel.accentLight
                    opacity: player.hasTrack ? 1 : 0.4
                    onClicked: panel.toggleLike()
                }
                IconHit {
                    icon: player.muted || player.volume < 0.01 ? "mute" : "volume"
                    glyphSize: 17
                    color: panel.accent
                    hoverColor: panel.accentLight
                    onClicked: player.toggleMute()
                }
            }
        }

        Item { width: 1; height: 12 }

        // â”€â”€ Scrubber + timestamps â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            id: seek
            width: parent.width
            height: 30

            property real dragRatio: 0
            property bool dragging: false
            readonly property bool enabled2: player.hasTrack && player.duration > 0

            Item {
                id: seekTrack
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                y: 4
                height: seekMouse.containsMouse || seek.dragging ? 5 : 3
                Behavior on height { NumberAnimation { duration: 120 } }

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: "#26232f"
                }
                Rectangle {
                    width: Math.max(height, panel.progressRatio * parent.width)
                    visible: panel.progressRatio > 0
                    height: parent.height
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: panel.accentDeep }
                        GradientStop { position: 1.0; color: panel.accent }
                    }
                }
                Rectangle {
                    width: seekMouse.containsMouse || seek.dragging ? 12 : 9
                    height: width
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(parent.width - width, panel.progressRatio * parent.width - width / 2))
                    color: "#ffffff"
                    visible: seek.enabled2
                    Behavior on width { NumberAnimation { duration: 120 } }
                }

                MouseArea {
                    id: seekMouse
                    anchors.fill: parent
                    anchors.topMargin: -10
                    anchors.bottomMargin: -10
                    enabled: seek.enabled2
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    preventStealing: true
                    function ratioAt(x) { return Math.max(0, Math.min(1, x / width)) }
                    onPressed: (mouse) => {
                        seek.dragRatio = ratioAt(mouse.x)
                        seek.dragging = true
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            seek.dragRatio = ratioAt(mouse.x)
                    }
                    // Seek once on release instead of on every pixel of drag.
                    onReleased: (mouse) => {
                        seek.dragRatio = ratioAt(mouse.x)
                        player.seek(Math.round(seek.dragRatio * player.duration))
                        seek.dragging = false
                    }
                    onCanceled: seek.dragging = false
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.bottom: parent.bottom
                text: panel.fmtClock(panel.displayPosition)
                color: panel.textFaint
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                anchors.bottom: parent.bottom
                text: panel.fmtClock(panel.displayDuration)
                color: panel.textFaint
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }

        // â”€â”€ Transport: shuffle Â· prev Â· PLAY Â· next Â· repeat â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            width: parent.width
            height: 82

            IconHit {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "shuffle"
                glyphSize: 17
                color: player.shuffle ? panel.accent : panel.textFaint
                hoverColor: player.shuffle ? panel.accentLight : "#ffffff"
                onClicked: player.shuffle = !player.shuffle
            }

            IconHit {
                anchors.right: playBtn.left
                anchors.rightMargin: 38
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                icon: "prev"
                glyphSize: 22
                color: "#f3f4f6"
                onClicked: player.previous()
            }

            Item {
                id: playBtn
                anchors.centerIn: parent
                width: 52
                height: 52

                Rectangle {
                    id: playCircle
                    anchors.fill: parent
                    radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: playHit.containsMouse ? "#b56cf8" : "#a855f7" }
                        GradientStop { position: 1.0; color: playHit.containsMouse ? "#8b47f0" : "#8b3df0" }
                    }
                    scale: playHit.pressed ? 0.92 : (playHit.containsMouse ? 1.04 : 1)
                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }

                    Glyph {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: !player.hasTrack || panel.playing ? 0 : 1.5
                        name: !player.hasTrack ? "plus" : (panel.playing ? "pause" : "play")
                        size: player.hasTrack ? 22 : 20
                        stroke: 2.4
                        color: "#ffffff"
                    }
                }

                MouseArea {
                    id: playHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!player.hasTrack)
                            panel.addRequested()
                        else
                            player.toggle()
                    }
                }
            }

            IconHit {
                anchors.left: playBtn.right
                anchors.leftMargin: 38
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                icon: "next"
                glyphSize: 22
                color: "#f3f4f6"
                onClicked: player.next()
            }

            IconHit {
                id: repeatBtn
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "repeat"
                glyphSize: 17
                color: player.repeatMode > 0 ? panel.accent : panel.textFaint
                hoverColor: player.repeatMode > 0 ? panel.accentLight : "#ffffff"
                onClicked: player.cycleRepeat()

                // "1" badge for repeat-one.
                Rectangle {
                    visible: player.repeatMode === 2
                    x: parent.width - width - 3
                    y: 3
                    width: 11
                    height: 11
                    radius: 5.5
                    color: panel.accent
                    Text {
                        anchors.centerIn: parent
                        text: "1"
                        color: "#ffffff"
                        font.family: t.font
                        font.pixelSize: 8
                        font.weight: Font.Black
                    }
                }
            }
        }

        // â”€â”€ Queue (toggled from the Â·Â·Â· menu) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        Item {
            id: queueBox
            width: parent.width
            height: panel.queueOpen && player.count > 0 ? Math.min(queueList.contentHeight, 176) + 18 : 0
            clip: true
            Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            Rectangle {
                x: panel.pad
                width: parent.width - 2 * panel.pad
                height: 1
                color: "#1d1a26"
            }

            ListView {
                id: queueList
                x: 12
                y: 10
                width: parent.width - 24
                height: parent.height - 18
                clip: true
                model: player.tracks
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: qRow
                    required property int index
                    required property var modelData
                    width: queueList.width
                    height: 36
                    readonly property bool current: index === player.index

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: qRow.current ? "#1c1430" : "#ffffff"
                        opacity: qRow.current ? 1 : (qRowHit.containsMouse ? 0.05 : 0)
                    }

                    Text {
                        id: qNum
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        text: qRow.index + 1
                        color: qRow.current ? panel.accent : panel.textFaint
                        font.family: t.font
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                    }

                    Text {
                        anchors.left: qNum.right
                        anchors.leftMargin: 6
                        anchors.right: qRemove.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: qRow.modelData.title + (qRow.modelData.artist ? "  Â·  " + qRow.modelData.artist : "")
                        color: qRow.current ? panel.accentLight : "#d1d5db"
                        elide: Text.ElideRight
                        font.family: t.font
                        font.pixelSize: 12
                        font.weight: qRow.current ? Font.Bold : Font.Normal
                    }

                    MouseArea {
                        id: qRowHit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: player.setIndex(qRow.index)
                    }

                    IconHit {
                        id: qRemove
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 28
                        icon: "close"
                        glyphSize: 12
                        stroke: 2
                        opacity: qRowHit.containsMouse || hovered ? 1 : 0
                        onClicked: player.remove(qRow.index)
                    }
                }
            }
        }

        Item { width: 1; height: 14 }
    }

    // ---------------------------------------------------------- Â·Â·Â· menu
    MouseArea {
        anchors.fill: parent
        visible: panel.menuOpen
        onClicked: panel.menuOpen = false
    }

    Glass {
        id: optMenu
        readonly property real targetY: 46
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: targetY + (panel.menuOpen ? 0 : -6)
        width: 176
        height: optCol.implicitHeight + 12
        radius: 14
        tint: "#121117"
        edge: "#262332"
        opacity: panel.menuOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 130 } }
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Column {
            id: optCol
            x: 6
            y: 6
            width: parent.width - 12

            MenuRow {
                width: parent.width
                implicitHeight: 34
                label: qsTr("Add songsâ€¦")
                icon: "plus"
                onTriggered: { panel.menuOpen = false; panel.addRequested() }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                visible: player.count > 0
                label: panel.queueOpen ? qsTr("Hide queue") : qsTr("Show queue")
                icon: "list"
                hint: player.count > 0 ? String(player.count) : ""
                onTriggered: { panel.menuOpen = false; panel.queueOpen = !panel.queueOpen }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                visible: player.count > 0
                label: qsTr("Clear queue")
                icon: "unpin"
                danger: true
                onTriggered: { panel.menuOpen = false; panel.queueOpen = false; player.clear() }
            }
        }
    }
}
