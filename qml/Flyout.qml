import QtQuick

// A glass card that drops out from under the notch. One spring drives it all:
// the card falls into place with a little overshoot, grows from 90% to 100%,
// and its content fades in a beat behind the glass so text never smears while
// the card is still scaling. Closing is quicker and does not bounce.
// The caller sets open, radius, baseY, x, width and height; whatever is
// declared inside becomes the card's content.
Item {
    id: fly

    property bool open: false
    property real radius: 22
    property real baseY: 0
    default property alias content: holder.data
    readonly property bool hovered: hover.hovered
    readonly property bool shown: opacity > 0.01

    // 0 = away, 1 = in place (overshoots a little while opening).
    property real p: open ? 1 : 0
    Behavior on p {
        SpringAnimation {
            spring: fly.open ? 3.4 : 5.6
            damping: fly.open ? 0.26 : 0.62
            epsilon: 0.0005
        }
    }
    readonly property real pc: Math.max(0, Math.min(1, p))

    y: baseY - 16 * (1 - p)
    opacity: Math.min(1, pc * 2.6)
    scale: 0.9 + 0.1 * p
    visible: opacity > 0.01
    transformOrigin: Item.Top

    HoverHandler { id: hover }

    Glass {
        id: bg
        anchors.fill: parent
        radius: fly.radius
    }

    Item {
        id: holder
        anchors.fill: parent
        opacity: Math.max(0, Math.min(1, (fly.pc - 0.2) * 2))
    }
}
