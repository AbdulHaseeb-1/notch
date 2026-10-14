import QtQuick

// Standalone music pill that sits beside the notch (separate from the app
// dock, as in the design):
//   - no song loaded:  a round [♫] button that opens the player card
//   - song loaded:     a capsule mini player [ |◀  ▶/⏸⏸  ▶| ] with a purple
//                      thin progress line
// Clicking the pill outside the buttons opens / closes the full player card.
Item {
    id: chip

    property real s: 1
    property bool active: false
    property real appear: 1
    property bool highlight: false      // e.g. while dragging files over it

    signal openRequested()
    signal volumeNudged(real volume)
    signal hintRequested(string text)
    signal hintCleared()

    Theme { id: t }

    readonly property bool hasTrack: player.hasTrack
    readonly property bool playing: player.state === 1
    readonly property real ratio: player.duration > 0
        ? Math.max(0, Math.min(1, player.position / player.duration)) : 0
    readonly property color accent: t.violet

    readonly property real pillH: Math.round(50 * s)
    implicitHeight: pillH
    implicitWidth: hasTrack ? Math.round(138 * s) : pillH
    height: pillH
    width: implicitWidth
    opacity: appear

    Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

    component TransportButton: Item {
        id: tb
        property string icon
        property real glyphSize: 18
        property real s: 1
        signal clicked()
        width: Math.round(34 * tb.s)
        height: width
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: width
            radius: width / 2
            color: t.ink
            opacity: tbHit.pressed ? 0.12 : (tbHit.containsMouse ? 0.07 : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Glyph {
            anchors.centerIn: parent
            name: tb.icon
            size: tb.glyphSize
            color: t.ink
            scale: tbHit.pressed ? 0.86 : 1
            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
        MouseArea {
            id: tbHit
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tb.clicked()
        }
    }

    // ---- surface
    Glass {
        id: bg
        anchors.fill: parent
        radius: height / 2
        edge: chip.highlight ? t.ink
            : chip.active ? t.violetDeep
            : (bgHover.hovered ? t.borderStrong : t.border)
        clip: true

        // Top hairline highlight.
        Rectangle {
            x: bg.radius * 0.8
            width: Math.max(0, parent.width - 2 * x)
            y: 1
            height: 1
            color: t.hairline
        }
    }

    // Background click target (below the buttons) - opens the full card.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.openRequested()
    }

    HoverHandler {
        id: bgHover
        onHoveredChanged: {
            if (hovered)
                chip.hintRequested(chip.hasTrack && player.title.length > 0
                                   ? player.title + (player.artist ? " — " + player.artist : "")
                                   : qsTr("Music"))
            else
                chip.hintCleared()
        }
    }

    // ---- idle: music note
    Glyph {
        anchors.centerIn: parent
        name: "notes"
        size: Math.round(22 * chip.s)
        color: chip.active ? t.violetLight : t.ink
        opacity: chip.hasTrack ? 0 : 1
        scale: chip.hasTrack ? 0.6 : 1
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
        Behavior on color { ColorAnimation { duration: 160 } }
    }

    // ---- mini player
    Row {
        id: controls
        anchors.centerIn: parent
        spacing: Math.round(4 * chip.s)
        opacity: chip.hasTrack ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 200 } }

        TransportButton {
            s: chip.s
            icon: "prev"
            glyphSize: Math.round(19 * chip.s)
            onClicked: player.previous()
        }
        TransportButton {
            s: chip.s
            icon: chip.playing ? "pause" : "play"
            glyphSize: Math.round(22 * chip.s)
            onClicked: player.toggle()
        }
        TransportButton {
            s: chip.s
            icon: "next"
            glyphSize: Math.round(19 * chip.s)
            onClicked: player.next()
        }
    }

    // Progress line along the bottom edge.
    Item {
        id: progress
        visible: chip.hasTrack && player.duration > 0
        opacity: controls.opacity
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(4 * chip.s)
        width: Math.round(parent.width * 0.62)
        height: 2

        Rectangle {
            anchors.fill: parent
            radius: 1
            color: t.track
        }
        Rectangle {
            width: Math.round(parent.width * chip.ratio)
            height: parent.height
            radius: 1
            color: chip.playing ? t.violetLight : t.textDim
            Behavior on color { ColorAnimation { duration: 200 } }
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
