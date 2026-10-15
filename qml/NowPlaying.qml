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

    Theme { id: t }

    readonly property bool playing: player.state === 1
    readonly property real pillH: Math.round(50 * s)
    readonly property real playingW: Math.round(80 * s)
    property bool barOpen: false
    property bool volumeOpen: false

    width: barOpen ? barRow.implicitWidth + Math.round(20 * s)
                   : (playing ? playingW : pillH)
    height: pillH
    opacity: appear

    Behavior on width { SpringAnimation { spring: 4.6; damping: 0.55; epsilon: 0.3 } }

    // One round transport button. Filled buttons (play/pause) are solid ink.
    component TransportButton: Item {
        id: tb
        property string icon
        property real glyphSize: Math.round(18 * chip.s)
        property color tint: t.ink
        property bool filled: false
        property bool badge: false
        signal clicked()

        width: Math.round(34 * chip.s)
        height: width

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: t.ink
            opacity: tb.filled ? (tbHit.pressed ? 0.85 : (tbHit.containsMouse ? 0.92 : 1))
                               : (tbHit.pressed ? 0.14 : (tbHit.containsMouse ? 0.08 : 0))
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: tb.icon
            size: tb.glyphSize
            color: tb.filled ? t.inverse : tb.tint
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
            color: t.ink
            Text {
                anchors.centerIn: parent
                text: "1"
                color: t.inverse
                font.family: t.font
                font.pixelSize: Math.round(8 * chip.s)
                font.weight: Font.Black
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
        edge: chip.highlight || chip.active ? t.ink : "transparent"
    }

    // Soft gradient wash across the pill while music plays.
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        opacity: chip.playing && !chip.barOpen ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 280 } }
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Qt.rgba(t.ink.r, t.ink.g, t.ink.b, 0.12) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Click target for the whole pill. It sits under the bar's buttons, so the
    // buttons take their own clicks.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            chip.barOpen = !chip.barOpen
            if (!chip.barOpen)
                chip.volumeOpen = false
        }
    }

    HoverHandler {
        id: pillHover
        onHoveredChanged: {
            if (hovered) {
                collapseTimer.stop()
                chip.hintRequested(player.hasTrack && player.title.length > 0
                                   ? player.title + (player.artist ? " — " + player.artist : "")
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

    // Stopped: the note alone. Playing: the note with a small level indicator
    // made of soft gradient bars.
    Row {
        id: idleRow
        anchors.centerIn: parent
        spacing: Math.round(7 * chip.s)
        opacity: chip.barOpen ? 0 : 1
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }

        Glyph {
            name: "notes"
            size: Math.round(20 * chip.s)
            color: t.ink
        }

        Item {
            id: meter
            visible: chip.playing
            width: Math.round(16 * chip.s)
            height: Math.round(20 * chip.s)

            Repeater {
                model: 4

                delegate: Rectangle {
                    id: bar

                    required property int index
                    property real level: 0.45

                    x: index * Math.round(5 * chip.s)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(3 * chip.s)
                    height: Math.round(20 * chip.s) * level
                    radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: t.ink }
                        GradientStop { position: 1.0; color: Qt.rgba(t.ink.r, t.ink.g, t.ink.b, 0.25) }
                    }

                    SequentialAnimation on level {
                        running: chip.playing && !chip.barOpen
                        loops: Animation.Infinite
                        NumberAnimation { to: 1.0; duration: 360 + bar.index * 120; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0.25; duration: 360 + bar.index * 120; easing.type: Easing.InOutSine }
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

        TransportButton {
            icon: "notes"
            tint: chip.active ? t.ink : t.textDim
            onClicked: chip.openRequested()
        }
        TransportButton {
            icon: "prev"
            glyphSize: Math.round(19 * chip.s)
            onClicked: player.previous()
        }
        TransportButton {
            icon: chip.playing ? "pause" : "play"
            filled: true
            glyphSize: Math.round(17 * chip.s)
            onClicked: player.toggle()
        }
        TransportButton {
            icon: "next"
            glyphSize: Math.round(19 * chip.s)
            onClicked: player.next()
        }
        TransportButton {
            icon: "repeat"
            tint: player.repeatMode > 0 ? t.ink : t.textFaint
            badge: player.repeatMode === 2
            onClicked: player.cycleRepeat()
        }
        TransportButton {
            icon: player.muted || player.volume < 0.01 ? "mute" : "volume"
            tint: chip.volumeOpen ? t.ink : t.textDim
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
                value: player.volume
                onMoved: (v) => {
                    player.volume = v
                    chip.volumeNudged(v)
                }
            }
        }
    }

    WheelHandler {
        target: null
        onWheel: (event) => {
            const next = Math.max(0, Math.min(1, player.volume + (event.angleDelta.y / 120) * 0.05))
            player.volume = next
            chip.volumeNudged(next)
        }
    }
}
