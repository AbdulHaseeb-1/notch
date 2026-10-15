import QtQuick

// One app in the notch: real app icon (mono or colour), hover magnification,
// launch bounce, running indicator.
Item {
    id: item

    required property int index
    required property var modelData

    property real s: 1
    property real cellW: 46
    property real iconSize: 30
    property real pointerX: -1          // pointer x in the parent row, -1 = none
    property real appear: 1             // 0..1 entrance progress
    property bool mono: true
    property bool running: false
    property bool menuActive: false

    signal activated(int index)
    signal contextRequested(int index)

    Theme { id: t }

    readonly property bool hovered: hover.hovered
    readonly property string iconSource: modelData.iconId
        ? "image://appicon/" + (mono ? t.iconMono : "color") + "/" + modelData.iconId : ""

    width: cellW
    implicitWidth: cellW

    // ---- hover magnification (gaussian falloff around the pointer)
    readonly property real centerX: x + width / 2
    readonly property real magnetTarget: pointerX < 0 ? 0
        : Math.exp(-Math.pow((pointerX - centerX) / (cellW * 0.95), 2))
    // Springy rather than eased: icons swell toward the pointer, overshoot a
    // touch, and settle, which is what makes the row feel alive.
    property real magnet: magnetTarget
    Behavior on magnet { SpringAnimation { spring: 6; damping: 0.36; epsilon: 0.001 } }

    // Press squash, also a spring so a quick tap still bounces back.
    property real press: tap.pressed ? 0.84 : 1
    Behavior on press { SpringAnimation { spring: 8; damping: 0.3; epsilon: 0.001 } }

    // ---- launch bounce
    property real jump: 0
    property bool launching: false

    function bounce() {
        launching = true
        bounceAnim.restart()
        launchTimeout.restart()
    }

    onRunningChanged: if (running) launching = false

    SequentialAnimation {
        id: bounceAnim
        NumberAnimation { target: item; property: "jump"; to: 1; duration: 170; easing.type: Easing.OutQuad }
        NumberAnimation { target: item; property: "jump"; to: 0; duration: 520; easing.type: Easing.OutBounce }
    }

    Timer {
        id: launchTimeout
        interval: 4000
        onTriggered: item.launching = false
    }

    // ---- visuals
    readonly property bool highlighted: menuActive || hovered

    // Rounded tile behind the hovered / menu-active icon (design: dark square).
    Rectangle {
        id: tile
        anchors.centerIn: iconBox
        width: Math.round(item.iconSize + 12 * item.s)
        height: width
        radius: Math.round(9 * item.s)
        color: item.menuActive ? t.tileActive : t.tile
        border.width: 1
        border.color: item.menuActive ? t.borderStrong : t.tileEdge
        opacity: item.highlighted ? item.appear : 0
        scale: item.highlighted ? 1 : 0.8
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on scale { SpringAnimation { spring: 6; damping: 0.34; epsilon: 0.001 } }
    }

    Item {
        id: iconBox
        width: item.iconSize
        height: item.iconSize
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -Math.round(2 * item.s)
                                      - item.magnet * 4 * item.s
                                      - item.jump * 10 * item.s
                                      - (1 - item.appear) * 10 * item.s

        opacity: item.appear
        scale: (0.55 + 0.45 * item.appear)
               * (1 + 0.34 * item.magnet)
               * item.press

        Image {
            id: icon
            anchors.fill: parent
            source: item.iconSource
            sourceSize: Qt.size(128, 128)
            asynchronous: false
            cache: true
            smooth: true
            mipmap: true
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
        }

        // Monogram fallback when an app has no readable icon.
        Rectangle {
            anchors.fill: parent
            visible: icon.status !== Image.Ready
            radius: Math.round(width * 0.3)
            color: t.raised
            border.width: 1
            border.color: t.border

            Text {
                anchors.centerIn: parent
                text: (item.modelData.name || "?").charAt(0).toUpperCase()
                color: t.text
                font.family: t.font
                font.pixelSize: Math.round(parent.height * 0.5)
                font.weight: Font.Bold
            }
        }
    }

    // Running / launching indicator.
    Rectangle {
        id: dot
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(parent.height / 2 + item.iconSize / 2 + 3 * item.s)
        height: Math.max(3, Math.round(3.5 * item.s))
        width: item.launching && !item.running ? height * 3 : height
        radius: height / 2
        color: item.running && item.highlighted ? t.success : t.ink
        property real shown: item.running || item.launching ? 1 : 0
        property real pulse: 1
        opacity: shown * pulse * item.appear
        Behavior on width { SpringAnimation { spring: 5; damping: 0.4; epsilon: 0.1 } }
        Behavior on shown { NumberAnimation { duration: 220 } }
        Behavior on color { ColorAnimation { duration: 160 } }

        SequentialAnimation on pulse {
            running: item.launching && !item.running
            loops: Animation.Infinite
            onRunningChanged: if (!running) dot.pulse = 1
            NumberAnimation { to: 0.3; duration: 420; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 420; easing.type: Easing.InOutSine }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint, button) => {
            if (button === Qt.RightButton) {
                item.contextRequested(item.index)
            } else {
                item.bounce()
                item.activated(item.index)
            }
        }
    }
}
