pragma ComponentBehavior: Bound
import QtQuick

// The music pill at the top edge of the desktop, which opens into a player card.
//   - Stopped: a round note icon.
//   - Playing: the pill minifies to a small capsule with a soft gradient level
//     indicator, and a faint gradient wash across it.
//   - Clicking the pill opens the transport bar: previous, play/pause, next,
//     repeat and volume. The volume icon opens a slider inside the bar.
//   - `expanded` (Main sets it while no app window sits in the pill's space): the
//     pill grows into a player card with the cover, title, artist, a seekable
//     progress bar and the transport. Size, corners and contents all follow one
//     `open` value, so they move together, and each row arrives a beat after the
//     one above it.
//   - `tucked` (an app window sits in the space): only a sliver of the card shows
//     under the top edge, and a click on it pops the card out.
//   - With nothing loaded a click just says how to get music going; right-click
//     opens Settings. Buddy queues and plays music when asked.
// The bar closes on a second click on empty pill space, or when the pointer
// leaves the pill (unless the volume slider is being dragged).
Item {
    id: chip

    property real s: 1
    property real appear: 1
    property bool highlight: false      // a file is being dragged over the pill
    property bool expanded: false       // grow into the full player card
    property bool tucked: false         // the card is tucked up to a sliver: a click pops it out

    signal emptyClicked()
    signal settingsRequested()
    signal volumeNudged(real volume)
    signal hintRequested(string text)
    signal hintCleared()
    signal popRequested()


    readonly property bool playing: Media.playing
    readonly property color dynAccent: Media.hasTrack ? Media.accentColor : Theme.ink
    readonly property real pillH: Math.round(50 * s)
    readonly property real playingW: Math.round((Media.hasCover ? 86 : 80) * s)
    readonly property real cardW: Math.round(340 * s)
    readonly property real cardH: Math.round(180 * s)
    readonly property real cardR: Math.round(30 * s)
    readonly property real pad: Math.round(18 * s)
    readonly property real artS: Math.round(64 * s)
    readonly property real rowB: pad + artS + Math.round(20 * s)     // the progress row
    readonly property real progress: Media.duration > 0 ? clamp01(Media.position / Media.duration) : 0
    property bool barOpen: false
    property bool volumeOpen: false

    function clamp01(v) { return Math.max(0, Math.min(1, v)) }

    // Where the pill is heading when it is not a card: the transport bar's width, the
    // cover, or a plain round pill. It is animated, so a bar opening widens smoothly.
    readonly property real restW: barOpen ? barRow.implicitWidth + Math.round(20 * s)
                                  : (playing || Media.hasCover ? playingW : pillH)
    property real baseW: restW
    Behavior on baseW { NumberAnimation { duration: 220; easing.type: Easing.OutQuint } }

    // 0 = the pill, 1 = the player card. Springs a little past the card on the way out.
    property real open: expanded ? 1 : 0
    Behavior on open {
        NumberAnimation {
            duration: chip.expanded ? 560 : 400
            easing.type: chip.expanded ? Easing.OutBack : Easing.InOutCubic
            easing.overshoot: 0.9
        }
    }

    // The space the pill takes once it is open. The window uses it to decide whether an
    // app is in the way, so it depends on the track only, never on `expanded` (that would
    // make the card and the check keep switching each other off).
    readonly property real footW: Media.hasTrack ? cardW : restW
    readonly property real footH: Media.hasTrack ? cardH : pillH

    width: baseW + (cardW - baseW) * open
    height: pillH + (cardH - pillH) * open
    opacity: appear
    clip: true

    onExpandedChanged: if (expanded) {
        barOpen = false
        volumeOpen = false
    }

    function timeText(ms) {
        const t = Math.max(0, Math.round(ms / 1000))
        const sec = t % 60
        return Math.floor(t / 60) + ":" + (sec < 10 ? "0" : "") + sec
    }

    function seekTo(fraction) {
        if (Media.canSeek)
            Media.seek(Math.round(clamp01(fraction) * Media.duration))
    }

    // One round transport button. Filled buttons (play/pause) are solid ink.
    component TransportButton: Item {
        id: tb
        property string icon
        property real glyphSize: Math.round(18 * chip.s)
        property color tint: Theme.ink
        property bool filled: false
        property bool badge: false
        signal clicked()

        width: Math.round(34 * chip.s)
        height: width

        Rectangle {
            visible: tb.filled
            anchors.fill: parent
            radius: width / 2
            color: Theme.ink
            opacity: tbHit.pressed ? 0.85 : 1
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: tb.icon
            size: tb.glyphSize
            color: tb.filled ? Theme.inverse : tb.tint
            scale: tbHit.pressed ? 0.86 : 1
            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
        // "1" badge for repeat-one.
        Rectangle {
            visible: tb.badge
            anchors.right: parent.right
            anchors.top: parent.top
            width: Math.round(11 * chip.s)
            height: width
            radius: width / 2
            color: chip.dynAccent
            Text {
                anchors.centerIn: parent
                text: "1"
                color: "#ffffff"
                font.family: Theme.font
                font.pixelSize: Math.round(8 * chip.s)
                font.weight: Font.Bold
            }
        }
        MouseArea {
            id: tbHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tb.clicked()
        }
    }

    // Four little level bars that dance while the music plays.
    component LevelMeter: Item {
        id: bars
        property real unit: 1
        property bool live: false
        width: Math.round(18 * bars.unit)
        height: Math.round(20 * bars.unit)

        // The bars step ~9 times a second instead of animating every
        // frame: the notch window is as large as the screen, so a 60 fps
        // animation here repainted all of it for as long as music played.
        property int tick: 0
        Timer {
            interval: 110
            repeat: true
            running: bars.live
            onTriggered: bars.tick += 1
        }

        Repeater {
            model: 4

            delegate: Rectangle {
                id: bar

                required property int index
                readonly property real level: !bars.live ? (chip.playing ? 0.45 : 0.22)
                    : 0.22 + 0.78 * (0.5 + 0.5 * Math.sin(bars.tick * (0.55 + bar.index * 0.17)
                                                          + bar.index * 1.7))

                x: index * Math.round(5 * bars.unit)
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(3 * bars.unit)
                height: Math.max(Math.round(4 * bars.unit), bars.height * level)
                radius: width / 2
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: chip.dynAccent
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                    GradientStop {
                        position: 1.0
                        color: Qt.rgba(chip.dynAccent.r, chip.dynAccent.g, chip.dynAccent.b, 0.32)
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                }
            }
        }
    }

    // Surface: a pill, growing into a card with the same glass.
    Glass {
        id: bg
        anchors.fill: parent
        radius: chip.pillH / 2 + (chip.cardR - chip.pillH / 2) * chip.open
        edge: chip.highlight ? chip.dynAccent : "transparent"
    }

    // Dynamic ambient wash across the pill while music plays or cover art is loaded.
    Rectangle {
        anchors.fill: parent
        radius: bg.radius
        opacity: (chip.playing || Media.hasCover) && !chip.barOpen && chip.open < 0.01 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 280 } }
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop {
                position: 0.5
                color: Qt.rgba(chip.dynAccent.r, chip.dynAccent.g, chip.dynAccent.b, chip.playing ? 0.20 : 0.10)
                Behavior on color { ColorAnimation { duration: 320 } }
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Click target for the whole pill: toggles the inline transport bar (or, with
    // nothing loaded, says how to get music going). Right-click opens Settings.
    // The card has its own buttons, declared after this, so they get the clicks.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (chip.tucked) {
                // Only the sliver shows: a click pops the whole card back out.
                chip.popRequested()
            } else if (mouse.button === Qt.RightButton) {
                chip.barOpen = false
                chip.volumeOpen = false
                chip.settingsRequested()
            } else if (chip.expanded) {
                // The card is open: nothing to toggle here.
            } else if (!Media.hasTrack) {
                chip.barOpen = false
                chip.volumeOpen = false
                chip.emptyClicked()
            } else {
                chip.barOpen = !chip.barOpen
                if (!chip.barOpen)
                    chip.volumeOpen = false
            }
        }
    }

    HoverHandler {
        id: pillHover
        onHoveredChanged: {
            if (hovered) {
                collapseTimer.stop()
                if (!chip.expanded)
                    chip.hintRequested(Media.hasTrack && Media.title.length > 0
                                       ? Media.title + (Media.artist ? " — " + Media.artist : "")
                                                 + (Media.sourceName ? " · " + Media.sourceName : "")
                                       : qsTr("Music"))
            } else {
                chip.hintCleared()
                collapseTimer.restart()
            }
        }
    }

    Timer {
        id: collapseTimer
        interval: 900
        onTriggered: {
            if (!pillHover.hovered && !volumeSlider.dragging) {
                chip.barOpen = false
                chip.volumeOpen = false
            }
        }
    }

    // The pill's own face: the note or cover with its level bars, or the transport bar.
    // It fades out as the card opens.
    Item {
        id: pillFace
        anchors.fill: parent
        opacity: 1 - chip.clamp01(chip.open * 3)
        visible: opacity > 0.01

        // Stopped: the note or cover art. Playing: cover art (or note) with dynamic
        // level bars colored to the active artwork/app palette.
        Row {
            id: idleRow
            anchors.centerIn: parent
            spacing: Math.round(8 * chip.s)
            opacity: chip.barOpen ? 0 : 1
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 160 } }

            Item {
                width: Media.hasCover ? Math.round(28 * chip.s) : Math.round(20 * chip.s)
                height: width
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    id: miniArtFrame
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.raised
                    border.width: 0
                    border.color: Qt.rgba(chip.dynAccent.r, chip.dynAccent.g, chip.dynAccent.b, 0.45)
                    visible: miniCover.visible
                    clip: true

                    Image {
                        id: miniCover
                        anchors.fill: parent
                        anchors.margins: 1
                        // Pre-rounded pixels from the provider: clip ignores
                        // radius, so a square crop would poke past this circle.
                        source: Media.hasCover ? Media.coverUrlRound : ""
                        sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectCrop
                        visible: Media.hasCover && status === Image.Ready
                        asynchronous: true
                        smooth: true
                    }
                }

                Glyph {
                    anchors.centerIn: parent
                    visible: !miniCover.visible
                    name: "notes"
                    size: Math.round(20 * chip.s)
                    color: Media.hasTrack ? chip.dynAccent : Theme.ink
                    Behavior on color { ColorAnimation { duration: 260 } }
                }
            }

            LevelMeter {
                id: pillMeter
                visible: chip.playing || Media.hasCover
                unit: chip.s
                live: chip.playing && !chip.barOpen && chip.open < 0.5 && chip.visible
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // The transport bar.
        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: Math.round(4 * chip.s)
            height: Math.round(34 * chip.s)
            opacity: chip.barOpen ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 160 } }

            // Open full player card button (shows mini cover when available).
            Item {
                width: Math.round(34 * chip.s)
                height: width

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(26 * chip.s)
                    height: width
                    radius: Math.round(7 * chip.s)
                    color: Theme.raised
                    border.width: 0
                    border.color: Qt.rgba(chip.dynAccent.r, chip.dynAccent.g, chip.dynAccent.b, 0.5)
                    visible: barMiniCover.visible
                    clip: true

                    Image {
                        id: barMiniCover
                        anchors.fill: parent
                        anchors.margins: 1
                        // Same trick: the provider hands over rounded corners, so
                        // the image cannot out-stick its rounded frame.
                        source: Media.hasCover ? Media.coverUrlRound : ""
                        sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectCrop
                        visible: Media.hasCover && status === Image.Ready
                        asynchronous: true
                        smooth: true
                    }
                }

                Glyph {
                    anchors.centerIn: parent
                    visible: !barMiniCover.visible
                    name: "notes"
                    size: Math.round(18 * chip.s)
                    color: Theme.textDim
                }
            }

            TransportButton {
                icon: "prev"
                glyphSize: Math.round(19 * chip.s)
                tint: Media.canPrev ? Theme.ink : Theme.textFaint
                onClicked: if (Media.canPrev) Media.previous()
            }
            TransportButton {
                icon: chip.playing ? "pause" : "play"
                filled: true
                glyphSize: Math.round(17 * chip.s)
                onClicked: Media.toggle()
            }
            TransportButton {
                icon: "next"
                glyphSize: Math.round(19 * chip.s)
                tint: Media.canNext ? Theme.ink : Theme.textFaint
                onClicked: if (Media.canNext) Media.next()
            }
            TransportButton {
                visible: Media.canRepeat
                icon: "repeat"
                tint: Media.repeatMode > 0 ? chip.dynAccent : Theme.textFaint
                badge: Media.repeatMode === 2
                onClicked: Media.cycleRepeat()
            }
            TransportButton {
                icon: Media.muted || Media.volume < 0.01 ? "mute" : "volume"
                tint: chip.volumeOpen ? chip.dynAccent : Theme.textDim
                onClicked: chip.volumeOpen = !chip.volumeOpen
            }
            Item {
                visible: chip.volumeOpen
                width: Math.round(92 * chip.s)
                height: Math.round(34 * chip.s)

                SizeSlider {
                    id: volumeSlider
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    from: 0
                    to: 1
                    value: Media.volume
                    onMoved: (v) => {
                        Media.setVolume(v)
                        chip.volumeNudged(v)
                    }
                }
            }
        }
    }

    // The player card the pill opens into. Laid out at its full size and centred, so the
    // surface reveals it as it grows, and it drops into place as it opens.
    Item {
        id: card
        width: chip.cardW
        height: chip.cardH
        x: Math.round((chip.width - width) / 2)
        y: Math.round((chip.height - height) / 2)
        opacity: chip.clamp01(chip.open * 1.6 - 0.3)
        visible: opacity > 0.01
        scale: 0.94 + 0.06 * chip.clamp01(chip.open)

        // Each group arrives a beat after the one above it.
        readonly property real headA: chip.clamp01((chip.open - 0.2) / 0.45)
        readonly property real seekA: chip.clamp01((chip.open - 0.35) / 0.45)
        readonly property real ctrlA: chip.clamp01((chip.open - 0.5) / 0.45)

        // Cover art.
        Rectangle {
            id: cardArt
            x: chip.pad
            y: chip.pad
            width: chip.artS
            height: width
            radius: Math.round(14 * chip.s)
            color: Theme.raised
            opacity: card.headA
            transform: Translate { y: (1 - card.headA) * 8 * chip.s }

            Image {
                id: cardCover
                anchors.fill: parent
                // Pre-rounded square corners from the provider.
                source: Media.hasCover ? Media.coverUrlSoft : ""
                sourceSize: Qt.size(128, 128)
                fillMode: Image.PreserveAspectCrop
                visible: Media.hasCover && status === Image.Ready
                asynchronous: true
                smooth: true
            }

            Glyph {
                anchors.centerIn: parent
                visible: !cardCover.visible
                name: "notes"
                size: Math.round(28 * chip.s)
                color: chip.dynAccent
            }
        }

        // Title and artist, with the level bars beside them.
        Text {
            id: cardTitle
            x: cardArt.x + cardArt.width + Math.round(14 * chip.s)
            y: chip.pad + Math.round(8 * chip.s)
            width: cardMeter.x - x - Math.round(10 * chip.s)
            text: Media.title
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Math.round(17 * chip.s)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            opacity: card.headA
            transform: Translate { y: (1 - card.headA) * 8 * chip.s }
        }
        Text {
            x: cardTitle.x
            y: cardTitle.y + Math.round(24 * chip.s)
            width: cardTitle.width
            text: Media.artist
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Math.round(14 * chip.s)
            elide: Text.ElideRight
            opacity: card.headA
            transform: Translate { y: (1 - card.headA) * 8 * chip.s }
        }
        LevelMeter {
            id: cardMeter
            x: card.width - chip.pad - width
            y: chip.pad + Math.round((chip.artS - height) / 2)
            unit: chip.s
            live: chip.playing && chip.open > 0.5
            opacity: card.headA
        }

        // Elapsed | seekable track | remaining.
        Text {
            id: elapsedText
            x: chip.pad
            y: chip.rowB
            width: Math.round(40 * chip.s)
            height: Math.round(16 * chip.s)
            verticalAlignment: Text.AlignVCenter
            text: chip.timeText(Media.position)
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Math.round(11 * chip.s)
            opacity: card.seekA
        }
        Text {
            x: card.width - chip.pad - width
            y: chip.rowB
            width: Math.round(40 * chip.s)
            height: Math.round(16 * chip.s)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: "-" + chip.timeText(Math.max(0, Media.duration - Media.position))
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Math.round(11 * chip.s)
            opacity: card.seekA
        }
        Item {
            id: seekBar
            x: chip.pad + Math.round(46 * chip.s)
            y: chip.rowB
            width: card.width - 2 * (chip.pad + Math.round(46 * chip.s))
            height: Math.round(16 * chip.s)
            opacity: card.seekA

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Math.max(3, Math.round(4 * chip.s))
                radius: height / 2
                color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.18)
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * chip.progress
                height: Math.max(3, Math.round(4 * chip.s))
                radius: height / 2
                color: Theme.ink
            }
            MouseArea {
                anchors.fill: parent
                anchors.topMargin: -Math.round(6 * chip.s)
                anchors.bottomMargin: -Math.round(6 * chip.s)
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => chip.seekTo(mouse.x / width)
                onPositionChanged: (mouse) => {
                    if (pressed)
                        chip.seekTo(mouse.x / width)
                }
            }
        }

        // Transport: previous, play/pause, next, and mute at the right.
        Item {
            id: controls
            y: chip.rowB + Math.round(26 * chip.s)
            width: card.width
            height: Math.round(34 * chip.s)
            opacity: card.ctrlA
            transform: Translate { y: (1 - card.ctrlA) * 8 * chip.s }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Math.round(36 * chip.s)
                TransportButton {
                    icon: "prev"
                    glyphSize: Math.round(22 * chip.s)
                    tint: Media.canPrev ? Theme.ink : Theme.textFaint
                    onClicked: if (Media.canPrev) Media.previous()
                }
                TransportButton {
                    icon: chip.playing ? "pause" : "play"
                    glyphSize: Math.round(28 * chip.s)
                    onClicked: Media.toggle()
                }
                TransportButton {
                    icon: "next"
                    glyphSize: Math.round(22 * chip.s)
                    tint: Media.canNext ? Theme.ink : Theme.textFaint
                    onClicked: if (Media.canNext) Media.next()
                }
            }
            TransportButton {
                x: controls.width - chip.pad - width
                icon: Media.muted || Media.volume < 0.01 ? "mute" : "volume"
                glyphSize: Math.round(18 * chip.s)
                tint: Theme.textDim
                onClicked: Media.toggleMute()
            }
        }
    }

    WheelHandler {
        target: null
        onWheel: (event) => {
            const next = Math.max(0, Math.min(1, Media.volume + (event.angleDelta.y / 120) * 0.05))
            Media.setVolume(next)
            chip.volumeNudged(next)
        }
    }
}
