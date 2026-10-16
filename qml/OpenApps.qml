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
    readonly property bool hovered: hover.hovered && !menuShown

    signal activated(string exe)
    signal previewRequested(string exe, real sceneX)

    property bool menuShown: false
    property string menuExe: ""
    property string menuName: ""
    property real menuCenterX: 0

    // True while the small context menu hangs below a tile; Main adds its
    // rectangle to the window mask so the clicks actually land there.
    readonly property real menuW: 200
    readonly property real menuH: 96

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
                pointerX: open.hovered ? hover.point.position.x - tiles.x : -1

                onActivated: (i) => open.activated(open.apps[i].exe)
                // Right-click: these tiles are not pinned, so dock's rename /
                // unpin menu does not apply; a small show / close menu does.
                onContextRequested: (i) => {
                    open.menuExe = open.apps[i].exe
                    open.menuName = open.apps[i].name
                    open.menuCenterX = tiles.x + tile.x + tile.width / 2
                    open.menuShown = true
                }
                onHoveredChanged: {
                    if (hovered)
                        open.previewRequested(modelData.exe, tile.mapToItem(null, width / 2, 0).x)
                }
            }
        }
    }

    // Mask rectangle (root content coords) for Main's click-through mask and
    // glass shapes, so the hanging menu is actually interactive. Zero-sized
    // when nothing is shown.
    function menuRectIn(item) {
        if (!menuShown)
            return Qt.rect(0, 0, 0, 0)
        const p = mapToItem(item, appMenu.x, appMenu.y)
        return Qt.rect(Math.max(0, Math.round(p.x)), Math.max(0, Math.round(p.y)),
                       Math.round(open.menuW), Math.round(open.menuH))
    }

    Glass {
        id: appMenu
        x: Math.round(Math.max(12 - open.x, open.menuCenterX - open.menuW / 2))
        y: open.height + 6
        width: open.menuW
        height: open.menuH
        radius: 16
        visible: open.menuShown
        opacity: open.menuShown ? 1 : 0
        z: 2
        Behavior on opacity { NumberAnimation { duration: 110 } }

        Column {
            anchors.fill: parent
            anchors.margins: 6

            MenuRow {
                width: parent.width
                label: qsTr("Show")
                icon: "open"
                onTriggered: {
                    open.activated(open.menuExe)
                    open.menuShown = false
                }
            }
            MenuRow {
                width: parent.width
                label: qsTr("Close all its windows")
                icon: "close"
                danger: true
                onTriggered: {
                    openApps.closeApp(open.menuExe)
                    open.menuShown = false
                }
            }
        }
    }

    // Clicking anywhere else closes the tile menu.
    MouseArea {
        enabled: open.menuShown
        z: 1
        anchors.fill: parent
        onClicked: open.menuShown = false
    }
}
