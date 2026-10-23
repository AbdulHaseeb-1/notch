pragma ComponentBehavior: Bound
import QtQuick

// The ring of light round the orb, drawn by one fragment shader
// (shaders/buddyring.frag) instead of the stacks of Shape layers in BuddyRing.
// Same inputs and the same eight styles; BuddyRing switches to this when its
// `shader` property is set (Settings > Look & glow > GPU ring).
//
// One clock drives every style. It advances by the frame time times the ring
// speed, so a speed change bends the motion instead of jumping it.
Item {
    id: root

    property string style: "neon"
    property real midR: 26
    property real thickness: 2.4
    property real s: 1
    property var stops: ["#3ee8ff", "#8b6cff", "#ff4fd8", "#ffb36b"]
    property real speed: 1
    property bool running: true
    property real glow: 1

    readonly property var styleIds: ["neon", "aurora", "comet", "orbit", "dashes", "pulse", "prism", "halo"]

    // Seconds of ring time. Wrapped long before float precision matters (a hiccup
    // in the motion once every two hours of an on-screen ring).
    property real clock: 0

    FrameAnimation {
        running: root.running
        onTriggered: {
            root.clock += frameTime * root.speed
            if (root.clock > 7200)
                root.clock -= 7200
        }
    }

    ShaderEffect {
        id: fx
        // Room for the widest glow, the pulse waves and the halo's breathing.
        readonly property real margin: Math.ceil(24 * root.s)
        anchors.centerIn: parent
        width: root.width + 2 * margin
        height: width

        property color c0: Qt.color(root.stops[0])
        property color c1: Qt.color(root.stops[1])
        property color c2: Qt.color(root.stops[2])
        property color c3: Qt.color(root.stops[3])
        property real quad: width
        property real midR: root.midR
        property real thick: root.thickness
        property real s: root.s
        property real glow: root.glow
        property real time: root.clock
        property int style: Math.max(0, root.styleIds.indexOf(root.style))

        vertexShader: "qrc:/shaders/buddyring.vert.qsb"
        fragmentShader: "qrc:/shaders/buddyring.frag.qsb"
    }
}
