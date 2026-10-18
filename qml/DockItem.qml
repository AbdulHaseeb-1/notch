import QtQuick
import QtQuick.Window

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


    readonly property bool hovered: hover.hovered
    readonly property string iconSource: modelData.iconId
        ? "image://appicon/" + (mono ? Theme.iconMono : "color") + "/" + modelData.iconId : ""

    width: cellW
    implicitWidth: cellW

    // ---- hover magnification (gaussian falloff around the pointer)
    readonly property real centerX: x + width / 2
    readonly property real magnetTarget: pointerX < 0 ? 0
        : Math.exp(-Math.pow((pointerX - centerX) / (cellW * 0.9), 2))
    property real magnet: magnetTarget
    Behavior on magnet { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    // Press squash
    property real press: tap.pressed ? 0.88 : 1
    Behavior on press { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

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
        NumberAnimation { target: item; property: "jump"; to: 1; duration: 150; easing.type: Easing.OutQuad }
        NumberAnimation { target: item; property: "jump"; to: 0; duration: 380; easing.type: Easing.OutBack }
    }

    Timer {
        id: launchTimeout
        interval: 4000
        onTriggered: item.launching = false
    }

    // ---- visuals
    readonly property bool highlighted: menuActive || hovered

    Item {
        id: iconBox
        width: item.iconSize
        height: item.iconSize
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -Math.round(1.5 * item.s)
                                      - item.magnet * 2.5 * item.s
                                      - item.jump * 6 * item.s
                                      - (1 - item.appear) * 6 * item.s

        opacity: item.appear
        scale: (0.7 + 0.3 * item.appear)
               * (1 + 0.22 * item.magnet)
               * item.press

        Image {
            id: icon
            anchors.fill: parent
            source: item.iconSource
            // Decode at the real on-screen pixel size: the icon's size, the
            // hover magnification (up to 1.22x) and the display scale, rounded
            // up to 16 px steps so resizing doesn't churn the image cache.
            readonly property int px: Math.max(48, Math.ceil(item.iconSize * 1.25
                                                         * Math.max(1, Screen.devicePixelRatio) / 16) * 16)
            sourceSize: Qt.size(px, px)
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
            color: Theme.raised
            border.width: 1
            border.color: Theme.border

            Text {
                anchors.centerIn: parent
                text: (item.modelData.name || "?").charAt(0).toUpperCase()
                color: Theme.text
                font.family: Theme.font
                font.pixelSize: Math.round(parent.height * 0.5)
                font.weight: Font.Bold
            }
        }
    }

    // Running / launching indicator.
    Rectangle {
        id: dot
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(4 * item.s)
        height: Math.max(3, Math.round(3.5 * item.s))
        width: item.launching && !item.running ? height * 2.8 : (item.running && item.highlighted ? height * 1.6 : height)
        radius: height / 2
        color: item.running && item.highlighted ? Theme.success : Theme.ink
        property real shown: item.running || item.launching ? 1 : 0
        property real pulse: 1
        opacity: shown * pulse * item.appear
        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on shown { NumberAnimation { duration: 180 } }
        Behavior on color { ColorAnimation { duration: 150 } }

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
