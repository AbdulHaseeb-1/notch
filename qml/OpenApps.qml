import QtQuick

// Programs with a window open, as their own glass pill beside the notch (the
// mirror of the music pill). Pinned apps stay in the notch itself.
Item {
    id: open

    property real s: 1
    property bool mono: true
    property real appear: 1
    property var apps: []

    readonly property real cellW: Math.round(44 * s)
    readonly property real iconSize: Math.round(24 * s)
    readonly property real padX: Math.round(10 * s)
    readonly property bool hasApps: apps.length > 0
    readonly property bool hovered: hover.hovered

    signal activated(string exe)
    signal previewRequested(string exe, real sceneX)

    implicitWidth: hasApps ? tiles.width + 2 * padX : 0
    width: implicitWidth
    height: Math.round(50 * s)
    opacity: hasApps ? appear : 0
    visible: opacity > 0.01

    Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

    Glass {
        anchors.fill: parent
        radius: height / 2
    }

    HoverHandler { id: hover }

    Row {
        id: tiles
        anchors.centerIn: parent
        height: parent.height

        Repeater {
            model: open.apps

            delegate: DockItem {
                id: tile
                height: tiles.height
                s: open.s
                cellW: open.cellW
                iconSize: open.iconSize
                mono: open.mono
                running: true
                pointerX: hover.hovered ? hover.point.position.x - tiles.x : -1

                onActivated: (i) => open.activated(open.apps[i].exe)
                onHoveredChanged: {
                    if (hovered)
                        open.previewRequested(modelData.exe, tile.mapToItem(null, width / 2, 0).x)
                }
            }
        }
    }
}
