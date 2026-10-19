pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: mb
    readonly property bool listening: Voice.listening
    readonly property bool working: Voice.busy
    visible: Voice.enabled
    width: visible ? 28 : 0
    height: 28

    Rectangle {
        anchors.centerIn: parent
        width: 26
        height: 26
        radius: 13
        color: mb.listening ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.22)
                            : (micHit.containsMouse ? Theme.hover : "transparent")
        border.width: mb.listening ? 1 : 0
        border.color: Theme.danger
        scale: mb.listening ? 1 + 0.35 * Math.min(1, Voice.level) : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Glyph {
        anchors.centerIn: parent
        name: Voice.configured ? "mic" : "mic-off"
        size: 14
        stroke: 1.7
        color: mb.listening ? Theme.danger : (mb.working ? Theme.accent : (micHit.containsMouse ? Theme.ink : Theme.textFaint))
        opacity: mb.working ? 0.55 : 1
    }
    MouseArea {
        id: micHit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Hold to talk")
        Accessible.description: Voice.error.length > 0 ? Voice.error
                                : (Voice.configured ? "" : qsTr("Voice input needs a speech service: Settings > Buddy"))
        onPressed: Voice.beginListening()
        onReleased: Voice.endListening()
        onCanceled: Voice.cancel()
    }
}
