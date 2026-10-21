pragma ComponentBehavior: Bound
import QtQuick

// The microphone in Buddy's bubble. With live conversation on, a click starts or
// ends a hands-free conversation; otherwise it is hold-to-talk (push-to-talk).
Item {
    id: mb
    readonly property bool live: Live.enabled
    readonly property bool listening: live ? Live.active && Live.state === "listening" && !Live.muted : Voice.listening
    readonly property bool working: live ? Live.active && (Live.state === "connecting" || Live.state === "thinking") : Voice.busy
    readonly property bool ready: live ? Live.problem.length === 0 : Voice.configured
    readonly property real level: live ? Live.level : Voice.level
    visible: Voice.enabled || Live.enabled
    width: visible ? 28 : 0
    height: 28

    Rectangle {
        anchors.centerIn: parent
        width: 26
        height: 26
        radius: 13
        color: mb.listening ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.22)
                            : (mb.live && Live.active ? Theme.tileActive
                               : (micHit.containsMouse ? Theme.hover : "transparent"))
        border.width: mb.listening || (mb.live && Live.active) ? 1 : 0
        border.color: mb.listening ? Theme.danger : Theme.accent
        scale: mb.listening ? 1 + 0.35 * Math.min(1, mb.level) : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Glyph {
        anchors.centerIn: parent
        name: mb.ready && !(mb.live && Live.muted) ? "mic" : "mic-off"
        size: 14
        stroke: 1.7
        color: mb.listening ? Theme.danger
               : (mb.working || (mb.live && Live.active) ? Theme.accent
                  : (micHit.containsMouse ? Theme.ink : Theme.textFaint))
        opacity: mb.working ? 0.55 : 1
    }
    MouseArea {
        id: micHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: mb.live ? (Live.active ? qsTr("End the conversation") : qsTr("Start a conversation")) : qsTr("Hold to talk")
        Accessible.description: mb.live ? (Live.error.length > 0 ? Live.error : Live.problem)
                                        : (Voice.error.length > 0 ? Voice.error
                                           : (Voice.configured ? "" : qsTr("Voice input needs a speech service: Settings > Buddy")))
        onPressed: if (!mb.live) Voice.beginListening()
        onReleased: if (!mb.live) Voice.endListening()
        onCanceled: if (!mb.live) Voice.cancel()
        onClicked: if (mb.live) Live.toggle()
    }
}
