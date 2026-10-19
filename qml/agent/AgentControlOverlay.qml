pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

// Everything shown while Buddy uses the computer:
//  * a glow around every screen (click-through),
//  * Buddy's own cursor over the display it controls (click-through),
//  * the control HUD under the notch with Stop / Resume.
// The overlays hide themselves for the instant a screenshot is taken and are
// also excluded from capture on Windows, so the model never sees them.
Item {
    id: overlay

    // Where the HUD goes: horizontally centred on hudCenterX, top at hudTop
    // (global logical coordinates, set by Main.qml from the notch).
    property real hudCenterX: Screen.virtualX + Screen.width / 2
    property real hudTop: Screen.virtualY + 64
    property var hudScreen: null

    readonly property bool engaged: AgentControl.active
    readonly property bool hiddenForShot: AgentControl.capturing
    readonly property rect display: AgentControl.displayGeometry

    function screenFor(r) {
        // Window.screen needs a ScreenInfo from Application.screens (Qt 6.12:
        // assigning a raw QScreen* to it crashes), so match by virtual-desktop
        // geometry instead of handing out AgentControl's QScreen objects.
        const list = Application.screens
        for (let i = 0; i < list.length; ++i)
            if (list[i].virtualX === r.x && list[i].virtualY === r.y)
                return list[i]
        return list.length > 0 ? list[0] : null
    }

    Instantiator {
        model: AgentControl.screens
        delegate: AgentGlow {
            required property var modelData
            targetScreen: modelData
            engaged: overlay.engaged && AgentControl.showGlow && !overlay.hiddenForShot
            intensity: AgentControl.glowIntensity
            style: AgentControl.glowStyle
        }
    }

    Window {
        id: cursorWindow
        screen: overlay.screenFor(overlay.display)
        x: overlay.display.x
        y: overlay.display.y
        width: Math.max(1, overlay.display.width)
        height: Math.max(1, overlay.display.height)
        color: "transparent"
        title: "Noch agent cursor"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
               | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus | Qt.NoDropShadowWindowHint
        visible: overlay.engaged && AgentControl.showCursor && !overlay.hiddenForShot

        AgentCursor {
            anchors.fill: parent
            target: Qt.point(AgentControl.cursor.x - overlay.display.x, AgentControl.cursor.y - overlay.display.y)
            moveMs: AgentControl.moveMs
            clickSerial: AgentControl.clickSerial
            dragging: AgentControl.dragging
            dragFrom: Qt.point(AgentControl.dragFrom.x - overlay.display.x, AgentControl.dragFrom.y - overlay.display.y)
            typing: AgentControl.typing
            paused: AgentControl.paused
        }

        Component.onCompleted: AgentControl.excludeFromCapture(cursorWindow)
    }

    Window {
        id: hudWindow
        screen: overlay.hudScreen ? overlay.hudScreen
                                  : (Application.screens.length > 0 ? Application.screens[0] : null)
        // Fixed size (no resizes while text changes); only the pill itself
        // takes clicks, see setInputRect below.
        width: 680
        height: hud.implicitHeight + 24
        x: Math.round(overlay.hudCenterX - width / 2)
        y: Math.round(overlay.hudTop)
        color: "transparent"
        title: "Noch agent control"
        // Takes clicks (Stop / Resume) but never keyboard focus, so typing
        // keeps going to the app Buddy is working in.
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
               | Qt.WindowDoesNotAcceptFocus | Qt.NoDropShadowWindowHint
        visible: !overlay.hiddenForShot && (overlay.engaged || hud.opacity > 0.01)

        AgentHud {
            id: hud
            width: Math.min(implicitWidth, hudWindow.width - 24)
            height: implicitHeight
            x: Math.round((hudWindow.width - width) / 2)
            y: 12
            opacity: overlay.engaged ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
            paused: AgentControl.paused
            pauseReason: AgentControl.pauseReason
            actionLabel: AgentControl.actionLabel
            onStopClicked: AgentControl.stop()
            onResumeClicked: AgentControl.resume()

            function updateMask() {
                AgentControl.setInputRect(hudWindow, Qt.rect(x, y, width, height))
            }
            onXChanged: updateMask()
            onWidthChanged: updateMask()
            onHeightChanged: updateMask()
        }

        Component.onCompleted: {
            AgentControl.excludeFromCapture(hudWindow)
            hud.updateMask()
        }
    }
}
