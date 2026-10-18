import QtQuick
import QtQuick.Shapes

// Dynamic universal "NOW PLAYING" card:
//   - Compatible with Spotify, YouTube, Chrome, Edge, Firefox, Brave, VLC,
//     Apple Music, any Windows SMTC app, and Noch's built-in audio player.
//   - Extracts a live color palette (accentColor / ambientColor) from the
//     active track's cover art or falls back to the app's brand palette.
//   - Smoothly morphs the artwork bezel between 1:1 album square and 16:9
//     widescreen video frame based on the thumbnail aspect ratio.
//   - Includes a live source switcher strip when multiple media apps are open.
Item {
    id: panel

    signal closeRequested()
    signal addRequested()
    signal toastRequested(string text)
    // Folder/file drops go through Main's one ingestion path, like every
    // other drop target.
    signal dropForwarded(var drop)


    // ---- dynamic palette
    readonly property color cardBg:      Theme.surface
    readonly property color bezelBg:     Theme.raised
    readonly property color bezelBorder: Theme.border
    readonly property color dynAccent:   media.hasTrack ? media.accentColor : Theme.ink
    readonly property color dynAmbient:  media.hasTrack ? media.ambientColor : Theme.raised
    readonly property color accent:      dynAccent
    readonly property color accentLight: Qt.lighter(dynAccent, 1.15)
    readonly property color accentDeep:  Qt.darker(dynAccent, 1.28)
    readonly property color textMuted:   Theme.textDim
    readonly property color textFaint:   Theme.textFaint

    readonly property real pad: 22
    readonly property real cardRadius: 28

    property bool queueOpen: false
    property bool sourcesOpen: false
    property bool menuOpen: false
    property var liked: ({})
    // Keyed by artist and title: two live versions of "Same Song" (YouTube
    // and the album rip, say) are different things to like.
    function likeKey() {
        return (media.artist || "") + "\u001f" + (media.title || "")
    }
    readonly property bool isLiked: media.hasTrack && !!liked[likeKey()]

    // Helper for brand dot colors in the source switcher list
    function brandColorFor(kind) {
        if (kind === "spotify")    return "#1DB954"
        if (kind === "youtube")    return "#FF2644"
        if (kind === "chrome")     return "#4285F4"
        if (kind === "edge")       return "#0078D7"
        if (kind === "firefox")    return "#FF7139"
        if (kind === "brave")      return "#FB542B"
        if (kind === "opera")      return "#FF1B2D"
        if (kind === "vlc")        return "#FF8800"
        if (kind === "applemusic") return "#FA243C"
        if (kind === "soundcloud") return "#FF5500"
        return Theme.ink
    }

    function toggleLike() {
        if (!media.hasTrack)
            return
        const key = likeKey()
        const next = Object.assign({}, liked)
        if (next[key])
            delete next[key]
        else
            next[key] = true
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

    readonly property bool playing: media.playing
    readonly property string displayTitle: media.hasTrack
        ? (media.title || media.sourceName || qsTr("Playing"))
        : qsTr("Nothing playing")
    readonly property string displayArtist: media.hasTrack
        ? (media.artist
           ? (media.album && media.album !== media.artist
              ? media.artist + "  ·  " + media.album
              : media.artist)
           : (media.sourceName || qsTr("Unknown artist")))
        : qsTr("Play in Spotify, YouTube, Chrome, or drop audio here")
    readonly property real displayDuration: media.hasTrack ? media.duration : 0
    readonly property real displayPosition: seek.dragging ? seek.dragRatio * displayDuration : media.position
    readonly property real progressRatio: displayDuration > 0
        ? (seek.dragging ? seek.dragRatio : Math.max(0, Math.min(1, media.position / displayDuration)))
        : (panel.playing ? 1.0 : 0.0)

    // Dynamic artwork aspect ratio: 1.0 for square album art, up to ~1.78 for
    // YouTube / browser / video widescreen thumbnails.
    readonly property real effectiveAspect: media.hasCover
        ? Math.max(1.0, Math.min(1.78, media.coverAspect))
        : 1.0
    readonly property bool isWideArt: effectiveAspect > 1.18
    readonly property real artInnerH: isWideArt ? 142 : 146
    readonly property real artInnerW: Math.round(artInnerH * effectiveAspect)
    readonly property real artBezelW: artInnerW + 26
    readonly property real artBezelH: artInnerH + 26

    implicitWidth: 380
    readonly property real targetQueueHeight: panel.queueOpen && player.count > 0 && media.source !== "system"
                                              ? Math.min(player.count * 38 - 2, 176) + 18 : 0
    readonly property real targetHeight: body.implicitHeight - queueBox.height + targetQueueHeight
    implicitHeight: body.implicitHeight

    // Dynamic ambient radial wash at the top of the card, driven by the cover
    // artwork / source palette.
    Item {
        anchors.fill: parent
        clip: true

        Shape {
            anchors.fill: parent
            opacity: media.hasTrack ? (panel.playing ? 0.42 : 0.24) : 0.0
            Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }

            ShapePath {
                strokeWidth: -1
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: panel.width * 0.5
                    centerY: 118
                    centerRadius: panel.width * 0.68
                    focalX: centerX
                    focalY: 88
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b, 0.55)
                        Behavior on color { ColorAnimation { duration: 160 } }
                    }
                    GradientStop {
                        position: 0.52
                        color: Qt.rgba(panel.dynAmbient.r, panel.dynAmbient.g, panel.dynAmbient.b, 0.28)
                        Behavior on color { ColorAnimation { duration: 160 } }
                    }
                    GradientStop {
                        position: 1.0
                        color: "transparent"
                    }
                }
                startX: 0; startY: 0
                PathLine { x: panel.width; y: 0 }
                PathLine { x: panel.width; y: panel.height }
                PathLine { x: 0; y: panel.height }
                PathLine { x: 0; y: 0 }
            }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            // Same central ingestion as everywhere else: audio goes to the
            // player, app shortcuts pin, folders scan, unknowns share that
            // one toast — no silent failures here either.
            panel.dropForwarded(drop)
            drop.accept(Qt.CopyAction)
        }
    }

    component IconHit: Item {
        id: ih
        property string icon
        property real glyphSize: 16
        property color color: Theme.textDim
        property color hoverColor: Theme.ink
        property bool filled: false
        property real stroke: 1.8
        readonly property bool hovered: ihHit.containsMouse
        signal clicked()
        width: 32
        height: 32

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

        // ── Header: ∨   [ ● SOURCE ]   ··· ────────────────────────────────
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

            // Dynamic interactive source badge: shows active app/browser name,
            // live brand/cover color dot, and either toggles the multi-source
            // switcher or brings the active app window to the front.
            Rectangle {
                id: sourcePill
                anchors.centerIn: parent
                width: Math.min(parent.width - 116, sourcePillRow.implicitWidth + 22)
                height: 26
                radius: 13
                color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b,
                               panel.sourcesOpen ? 0.20 : 0.10)
                border.width: 1
                border.color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b,
                                      panel.sourcesOpen || sourcePillHit.containsMouse ? 0.48 : 0.22)
                Behavior on color { ColorAnimation { duration: 240 } }
                Behavior on border.color { ColorAnimation { duration: 180 } }

                Row {
                    id: sourcePillRow
                    anchors.centerIn: parent
                    spacing: 7

                    // Live status dot
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 3.5
                        anchors.verticalCenter: parent.verticalCenter
                        color: panel.dynAccent
                        Behavior on color { ColorAnimation { duration: 280 } }

                        SequentialAnimation on opacity {
                            running: panel.playing && panel.visible
                            loops: Animation.Infinite
                            // `on <property>` swaps the binding out while it
                            // runs; restore it when paused so the dot does not
                            // freeze at a random mid-fade value.
                            onRunningChanged: if (!running)
                                opacity = 1.0

                            NumberAnimation { to: 0.45; duration: 900; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // The pill itself is width-capped, so cap the label
                        // too — otherwise elide never engages and long source
                        // names spill out of the pill.
                        readonly property int maxLabelWidth: 176
                        width: Math.min(implicitWidth, maxLabelWidth)
                        text: media.sourceName && media.sourceName.length > 0
                              ? media.sourceName.toUpperCase()
                              : qsTr("NOW PLAYING")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.8
                        elide: Text.ElideRight
                    }

                    // Show chevron when multiple sources exist, or open-window arrow for external app
                    Glyph {
                        visible: media.sources.length > 1 || media.source === "system"
                        anchors.verticalCenter: parent.verticalCenter
                        name: media.sources.length > 1 ? "chevron-down" : "open"
                        size: 11
                        stroke: 2.0
                        color: panel.textMuted
                        rotation: media.sources.length > 1 && panel.sourcesOpen ? 180 : 0
                        Behavior on rotation { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }
                }

                MouseArea {
                    id: sourcePillHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (media.sources.length > 1)
                            panel.sourcesOpen = !panel.sourcesOpen
                        else if (media.source === "system")
                            media.activateSourceApp()
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                // Quick button to jump straight to the playing app window when
                // multiple sources exist (so the center pill toggles sources).
                IconHit {
                    visible: media.source === "system" && media.sources.length > 1
                    icon: "open"
                    glyphSize: 14
                    stroke: 1.9
                    color: panel.textMuted
                    hoverColor: panel.dynAccent
                    onClicked: media.activateSourceApp()
                }

                IconHit {
                    id: moreBtn
                    icon: "more"
                    glyphSize: 16
                    color: panel.menuOpen ? Theme.ink : panel.textMuted
                    onClicked: panel.menuOpen = !panel.menuOpen
                }
            }
        }

        // ── Multi-Source Switcher Strip (Spotify, YouTube, Chrome, VLC, Noch) ──
        Item {
            id: sourceStripBox
            width: parent.width
            readonly property bool shown: panel.sourcesOpen && media.sources.length > 1
            height: shown ? sourceStripRow.implicitHeight + 14 : 0
            opacity: shown ? 1 : 0
            visible: height > 0.5
            clip: true
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuint } }
            Behavior on opacity { NumberAnimation { duration: 160 } }

            Flickable {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                anchors.top: parent.top
                anchors.topMargin: 2
                height: sourceStripRow.implicitHeight + 4
                contentWidth: sourceStripRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: sourceStripRow
                    spacing: 6

                    Repeater {
                        model: media.sources

                        delegate: Rectangle {
                            id: srcChip
                            required property var modelData
                            // The C++ side marks the live source with
                            // `active` (no `selected` field exists), so this
                            // chip used to never get its highlight.
                            readonly property bool isCurrent: !!modelData.active
                            readonly property color chipAccent: panel.brandColorFor(modelData.kind)

                            width: srcChipInner.implicitWidth + 18
                            height: 26
                            radius: 13
                            color: isCurrent
                                   ? Qt.rgba(chipAccent.r, chipAccent.g, chipAccent.b, 0.18)
                                   : Theme.raised
                            border.width: 1
                            border.color: isCurrent
                                          ? Qt.rgba(chipAccent.r, chipAccent.g, chipAccent.b, 0.55)
                                          : Theme.border
                            Behavior on color { ColorAnimation { duration: 160 } }
                            Behavior on border.color { ColorAnimation { duration: 160 } }

                            Row {
                                id: srcChipInner
                                anchors.centerIn: parent
                                spacing: 6

                                Rectangle {
                                    width: 6
                                    height: 6
                                    radius: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: srcChip.chipAccent
                                    opacity: srcChip.modelData.playing || srcChip.isCurrent ? 1.0 : 0.55
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: srcChip.modelData.name
                                    color: srcChip.isCurrent ? Theme.ink : panel.textMuted
                                    font.family: Theme.font
                                    font.pixelSize: 11
                                    font.weight: srcChip.isCurrent ? Font.Bold : Font.Medium
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    media.selectSource(srcChip.modelData.id)
                                    panel.sourcesOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Adaptive Artwork Frame (1:1 Album or 16:9 Widescreen Video) ───
        Item {
            width: parent.width
            height: art.height + 4

            Item {
                id: art
                anchors.horizontalCenter: parent.horizontalCenter
                width: panel.artBezelW
                height: panel.artBezelH
                Behavior on width { NumberAnimation { duration: 280; easing.type: Easing.OutQuint } }
                Behavior on height { NumberAnimation { duration: 280; easing.type: Easing.OutQuint } }

                Rectangle {
                    id: bezel
                    anchors.fill: parent
                    radius: 18
                    color: panel.bezelBg
                    border.width: 1
                    border.color: media.hasTrack
                                  ? Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b, 0.28)
                                  : panel.bezelBorder
                    Behavior on border.color { ColorAnimation { duration: 320 } }

                    Rectangle {
                        id: artInner
                        anchors.centerIn: parent
                        width: panel.artInnerW
                        height: panel.artInnerH
                        radius: 10
                        color: "#0c0c0f"
                        clip: true
                        Behavior on width { NumberAnimation { duration: 280; easing.type: Easing.OutQuint } }
                        Behavior on height { NumberAnimation { duration: 280; easing.type: Easing.OutQuint } }

                        Image {
                            id: coverImg
                            anchors.fill: parent
                            source: media.hasCover ? media.coverUrl : ""
                            // Cover is shown down to ~180px wide circles; 256
                            // is plenty and keeps the texture light.
                            sourceSize: Qt.size(256, 256)
                            fillMode: Image.PreserveAspectCrop
                            visible: media.hasCover && status === Image.Ready
                            asynchronous: true
                            smooth: true
                            mipmap: true
                        }

                        // Default artwork: glowing polyhedron dynamically tinted
                        // with the active source/app palette when no cover is present.
                        Item {
                            id: defaultArt
                            anchors.fill: parent
                            visible: !coverImg.visible

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop {
                                        position: 0.0
                                        color: Qt.rgba(panel.dynAmbient.r * 0.65,
                                                       panel.dynAmbient.g * 0.65,
                                                       panel.dynAmbient.b * 0.65, 1.0)
                                        Behavior on color { ColorAnimation { duration: 140 } }
                                    }
                                    GradientStop { position: 1.0; color: "#0a0a0d" }
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
                                        centerRadius: Math.max(defaultArt.width, defaultArt.height) * 0.55
                                        focalX: centerX
                                        focalY: centerY
                                        GradientStop {
                                            position: 0.0
                                            color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b, 0.36)
                                            Behavior on color { ColorAnimation { duration: 140 } }
                                        }
                                        GradientStop {
                                            position: 0.55
                                            color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b, 0.12)
                                            Behavior on color { ColorAnimation { duration: 140 } }
                                        }
                                        GradientStop { position: 1.0; color: "transparent" }
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
                                width: Math.min(parent.width, parent.height) * 0.56
                                height: width
                                property real bob: 0
                                anchors.verticalCenterOffset: -3 * bob

                                SequentialAnimation on bob {
                                    running: panel.playing && defaultArt.visible && panel.visible
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
                                            GradientStop {
                                                position: 0.0
                                                color: Qt.rgba(panel.dynAccent.r, panel.dynAccent.g, panel.dynAccent.b, 0.55)
                                                Behavior on color { ColorAnimation { duration: 140 } }
                                            }
                                            GradientStop { position: 1.0; color: "#30707078" }
                                        }
                                        PathSvg { path: "M50 10 L85 30 L85 70 L50 90 L15 70 L15 30 Z" }
                                    }
                                    ShapePath {
                                        strokeWidth: -1
                                        strokeColor: "transparent"
                                        fillColor: "#55f2f2f5"
                                        PathSvg { path: "M50 32 L70 66 L30 66 Z" }
                                    }
                                    // edges
                                    ShapePath {
                                        strokeColor: "#f2f2f5"
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

                        // Clicking the artwork brings the external app to front,
                        // or opens file picker when empty.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: media.source === "system" || !media.hasTrack
                                         ? Qt.PointingHandCursor
                                         : Qt.ArrowCursor
                            onClicked: {
                                if (media.source === "system")
                                    media.activateSourceApp()
                                else if (!media.hasTrack)
                                    panel.addRequested()
                            }
                        }
                    }
                }
            }
        }

        Item { width: 1; height: 16 }

        // ── Title / artist + queue & heart ──────────────────────────────
        Item {
            width: parent.width
            height: 42

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
                    color: Theme.ink
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: panel.displayArtist
                    color: panel.textMuted
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            Row {
                id: metaActions
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                IconHit {
                    visible: player.count > 0 && media.source !== "system"
                    icon: "list"
                    glyphSize: 16
                    color: panel.queueOpen ? panel.accent : panel.textMuted
                    hoverColor: Theme.ink
                    onClicked: panel.queueOpen = !panel.queueOpen
                }

                IconHit {
                    icon: "heart"
                    glyphSize: 17
                    filled: panel.isLiked
                    color: panel.isLiked ? panel.accent : panel.textMuted
                    hoverColor: panel.accentLight
                    opacity: media.hasTrack ? 1 : 0.4
                    onClicked: panel.toggleLike()
                }
            }
        }

        Item { width: 1; height: 12 }

        // ── Scrubber + timestamps ───────────────────────────────────────
        Item {
            id: seek
            width: parent.width
            height: 30

            property real dragRatio: 0
            property bool dragging: false
            readonly property bool enabled2: media.hasTrack && media.canSeek && media.duration > 0

            Item {
                id: seekTrack
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                y: 4
                height: (seekMouse.containsMouse || seek.dragging) && seek.enabled2 ? 5 : 3
                Behavior on height { NumberAnimation { duration: 120 } }

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: Theme.track
                }
                Rectangle {
                    width: Math.max(height, panel.progressRatio * parent.width)
                    visible: panel.progressRatio > 0
                    height: parent.height
                    radius: height / 2
                    opacity: panel.displayDuration > 0 ? 1.0 : 0.45
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0.0
                            color: panel.accentDeep
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        GradientStop {
                            position: 1.0
                            color: panel.accent
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                    }
                }
                Rectangle {
                    width: seekMouse.containsMouse || seek.dragging ? 12 : 9
                    height: width
                    radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(parent.width - width, panel.progressRatio * parent.width - width / 2))
                    color: panel.accentLight
                    visible: seek.enabled2
                    Behavior on width { NumberAnimation { duration: 120 } }
                    Behavior on color { ColorAnimation { duration: 280 } }
                }

                MouseArea {
                    id: seekMouse
                    anchors.fill: parent
                    anchors.topMargin: -10
                    anchors.bottomMargin: -10
                    enabled: seek.enabled2
                    hoverEnabled: true
                    cursorShape: seek.enabled2 ? Qt.PointingHandCursor : Qt.ArrowCursor
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
                        media.seek(Math.round(seek.dragRatio * media.duration))
                        seek.dragging = false
                    }
                    onCanceled: seek.dragging = false
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad
                anchors.bottom: parent.bottom
                text: panel.displayDuration > 0
                      ? panel.fmtClock(panel.displayPosition)
                      : (media.hasTrack ? (panel.playing ? qsTr("LIVE") : qsTr("PAUSED")) : "0:00")
                color: panel.textFaint
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                anchors.bottom: parent.bottom
                text: panel.displayDuration > 0
                      ? panel.fmtClock(panel.displayDuration)
                      : (media.hasTrack && media.sourceName ? media.sourceName : "0:00")
                color: panel.textFaint
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }

        // ── Transport: shuffle · prev · PLAY · next · repeat ────────────
        Item {
            width: parent.width
            height: 76

            IconHit {
                anchors.left: parent.left
                anchors.leftMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "shuffle"
                glyphSize: 17
                opacity: media.canShuffle ? 1.0 : 0.35
                color: media.shuffle ? panel.accent : panel.textFaint
                hoverColor: media.shuffle ? panel.accentLight : Theme.accent
                onClicked: if (media.canShuffle) media.toggleShuffle()
            }

            IconHit {
                anchors.right: playBtn.left
                anchors.rightMargin: 38
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                icon: "prev"
                glyphSize: 22
                color: media.canPrev ? Theme.ink : panel.textFaint
                hoverColor: Theme.ink
                onClicked: if (media.canPrev) media.previous()
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
                        GradientStop {
                            position: 0.0
                            color: media.hasTrack ? panel.accent : Theme.ink
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        GradientStop {
                            position: 1.0
                            color: media.hasTrack ? panel.accentDeep : Theme.inkSoft
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                    }
                    scale: playHit.pressed ? 0.92 : (playHit.containsMouse ? 1.04 : 1)
                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }

                    Glyph {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: !media.hasTrack || panel.playing ? 0 : 1.5
                        name: !media.hasTrack ? "plus" : (panel.playing ? "pause" : "play")
                        size: media.hasTrack ? 22 : 20
                        stroke: 2.4
                        color: media.hasTrack ? "#ffffff" : Theme.inverse
                    }
                }

                MouseArea {
                    id: playHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!media.hasTrack)
                            panel.addRequested()
                        else
                            media.toggle()
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
                color: media.canNext ? Theme.ink : panel.textFaint
                hoverColor: Theme.ink
                onClicked: if (media.canNext) media.next()
            }

            IconHit {
                id: repeatBtn
                anchors.right: parent.right
                anchors.rightMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: "repeat"
                glyphSize: 17
                opacity: media.canRepeat ? 1.0 : 0.35
                color: media.repeatMode > 0 ? panel.accent : panel.textFaint
                hoverColor: media.repeatMode > 0 ? panel.accentLight : Theme.accent
                onClicked: if (media.canRepeat) media.cycleRepeat()

                // "1" badge for repeat-one.
                Rectangle {
                    visible: media.repeatMode === 2
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
                        font.family: Theme.font
                        font.pixelSize: 8
                        font.weight: Font.Black
                    }
                }
            }
        }

        // ── Universal Volume: controls system master volume for external apps
        // (Spotify, YouTube, Chrome, etc.) or player volume for internal audio.
        Item {
            width: parent.width
            height: 30

            IconHit {
                id: volumeIcon
                anchors.left: parent.left
                anchors.leftMargin: panel.pad - 8
                anchors.verticalCenter: parent.verticalCenter
                icon: media.muted || media.volume < 0.01 ? "mute" : "volume"
                glyphSize: 16
                color: panel.textMuted
                hoverColor: Theme.ink
                onClicked: media.toggleMute()
            }

            SizeSlider {
                anchors.left: volumeIcon.right
                anchors.leftMargin: 4
                anchors.right: volumeValue.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: 1
                value: media.volume
                onMoved: (v) => media.setVolume(v)
            }

            Text {
                id: volumeValue
                anchors.right: parent.right
                anchors.rightMargin: panel.pad
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                horizontalAlignment: Text.AlignRight
                text: Math.round(media.volume * 100) + "%"
                color: panel.textFaint
                font.family: Theme.font
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }

        // ── Queue (toggled from the ··· menu or list icon) ──────────────
        Item {
            id: queueBox
            width: parent.width
            height: panel.queueOpen && player.count > 0 && media.source !== "system"
                    ? Math.min(queueList.contentHeight, 176) + 18 : 0
            visible: height > 1
            clip: true
            Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            Rectangle {
                x: panel.pad
                width: parent.width - 2 * panel.pad
                height: 1
                color: Theme.border
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
                // Keep scrolled-out rows materialized: the queue is short, and
                // rebuilding delegates on the fly shows as a hitch mid-flick.
                cacheBuffer: 320

                delegate: Item {
                    id: qRow
                    required property int index
                    required property var modelData
                    width: queueList.width
                    height: 36
                    readonly property bool current: index === player.index

                    Text {
                        id: qNum
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        text: qRow.index + 1
                        color: qRow.current ? panel.accent : panel.textFaint
                        font.family: Theme.font
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
                        text: qRow.modelData.title + (qRow.modelData.artist ? "  ·  " + qRow.modelData.artist : "")
                        color: qRow.current ? panel.accentLight : Theme.text
                        elide: Text.ElideRight
                        font.family: Theme.font
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
                        // Invisible must also mean unclickable.
                        enabled: opacity > 0.5
                        onClicked: player.remove(qRow.index)
                    }
                }
            }
        }

        Item { width: 1; height: 14 }
    }

    // ---------------------------------------------------------- ··· menu
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
        width: 192
        height: optCol.implicitHeight + 12
        radius: 14
        tint: Theme.surface
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
                visible: media.source === "system" && media.sourceName.length > 0
                label: qsTr("Open %1").arg(media.sourceName)
                icon: "open"
                onTriggered: { panel.menuOpen = false; media.activateSourceApp() }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                visible: media.sources.length > 1
                label: panel.sourcesOpen ? qsTr("Hide media sources") : qsTr("Switch media source")
                icon: "globe"
                hint: String(media.sources.length)
                onTriggered: { panel.menuOpen = false; panel.sourcesOpen = !panel.sourcesOpen }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                label: qsTr("Add local songs…")
                icon: "plus"
                onTriggered: { panel.menuOpen = false; panel.addRequested() }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                visible: player.count > 0 && media.source !== "system"
                label: panel.queueOpen ? qsTr("Hide queue") : qsTr("Show queue")
                icon: "list"
                hint: player.count > 0 ? String(player.count) : ""
                onTriggered: { panel.menuOpen = false; panel.queueOpen = !panel.queueOpen }
            }
            MenuRow {
                width: parent.width
                implicitHeight: 34
                visible: player.count > 0 && media.source !== "system"
                label: qsTr("Clear queue")
                icon: "unpin"
                danger: true
                onTriggered: { panel.menuOpen = false; panel.queueOpen = false; player.clear() }
            }
        }
    }
}
