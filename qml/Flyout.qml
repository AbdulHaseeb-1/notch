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

    // 0 = away, 1 = in place. Critically-damped motion so cards glide smoothly
    // without oscillating or blurring text while settling.
    property real p: open ? 1 : 0
    Behavior on p {
        NumberAnimation {
            duration: fly.open ? 210 : 150
            easing.type: fly.open ? Easing.OutQuint : Easing.InCubic
        }
    }
    readonly property real pc: Math.max(0, Math.min(1, p))

    y: baseY - 10 * (1 - p)
    opacity: Math.min(1, pc * 1.8)
    scale: 0.95 + 0.05 * p
    visible: opacity > 0.005
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
        opacity: Math.max(0, Math.min(1, (fly.pc - 0.05) * 1.35))
    }
}
