pragma ComponentBehavior: Bound
import QtQuick

// Music pill beside the notch, separate from the app dock.
//   - Stopped: a round note icon.
//   - Playing: the pill minifies to a small capsule with a soft gradient level
//     indicator, and a faint gradient wash across it.
//   - Clicking the pill opens the transport bar: previous, play/pause, next,
//     repeat and volume. The volume icon opens a slider inside the bar. The
//     note at the start of the bar opens the full player card.
// The bar closes on a second click on empty pill space, or when the pointer
// leaves the pill (unless the volume slider is being dragged).
Item {
    id: chip

    property real s: 1
    property bool active: false         // the full player card is open
    property real appear: 1
    property bool highlight: false      // a file is being dragged over the pill

    signal openRequested()
    signal volumeNudged(real volume)
    signal hintRequested(string text)
    signal hintCleared()


    readonly property bool playing: Media.playing
    readonly property color dynAccent: Media.hasTrack ? Media.accentColor : Theme.ink
    readonly property real pillH: Math.round(50 * s)
    readonly property real playingW: Math.round((Media.hasCover ? 86 : 80) * s)
    property bool barOpen: false
    property bool volumeOpen: false

    width: barOpen ? barRow.implicitWidth + Math.round(20 * s)
                   : (playing || Media.hasCover ? playingW : pillH)
    height: pillH
    opacity: appear

    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutQuint } }

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

    // Surface.
    Glass {
        id: bg
        anchors.fill: parent
        radius: height / 2
        edge: chip.highlight || chip.active ? chip.dynAccent : "transparent"
    }

    // Dynamic ambient wash across the pill while music plays or cover art is loaded.
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        opacity: (chip.playing || Media.hasCover) && !chip.barOpen ? 1 : 0
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

    // Click target for the whole pill. When no track is loaded or the full
    // player card is open (or on right-click), toggle the full player card
    // directly; otherwise toggle the inline transport bar.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton || !Media.hasTrack || chip.active) {
                chip.barOpen = false
                chip.volumeOpen = false
                chip.openRequested()
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

        Item {
            id: meter
            visible: chip.playing || Media.hasCover
            width: Math.round(18 * chip.s)
            height: Math.round(20 * chip.s)
            anchors.verticalCenter: parent.verticalCenter

            // The bars step ~9 times a second instead of animating every
            // frame: the notch window is as large as the screen, so a 60 fps
            // animation here repainted all of it for as long as music played.
            readonly property bool live: chip.playing && !chip.barOpen && chip.visible
            property int tick: 0
            Timer {
                interval: 110
                repeat: true
                running: meter.live
                onTriggered: meter.tick += 1
            }

            Repeater {
                model: 4

                delegate: Rectangle {
                    id: bar

                    required property int index
                    readonly property real level: !meter.live ? (chip.playing ? 0.45 : 0.22)
                        : 0.22 + 0.78 * (0.5 + 0.5 * Math.sin(meter.tick * (0.55 + bar.index * 0.17)
                                                              + bar.index * 1.7))

                    x: index * Math.round(5 * chip.s)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(3 * chip.s)
                    height: Math.max(Math.round(4 * chip.s), Math.round(20 * chip.s) * level)
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
                color: chip.active ? chip.dynAccent : Theme.textDim
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: chip.openRequested()
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

    WheelHandler {
        target: null
        onWheel: (event) => {
            const next = Math.max(0, Math.min(1, Media.volume + (event.angleDelta.y / 120) * 0.05))
            Media.setVolume(next)
            chip.volumeNudged(next)
        }
    }
}
