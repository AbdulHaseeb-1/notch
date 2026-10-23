import QtQuick

// The live conversation caption: a pill under (or over) the orb with a dot that follows
// the voice and one line of what is going on. The line is the status ("Listening…"),
// what you just said, or what Buddy is saying. Buddy's words are set in the primary
// ink and keep the newest words in view as they stream in; a status is quieter. Click
// it to end the conversation.
Item {
    id: caption

    // Set by the owner (Buddy), so this stays a plain view of the live conversation.
    property real s: 1
    property bool shown: false
    property bool active: false
    property string liveState: ""      // Live.state: connecting | listening | thinking | speaking
    property bool muted: false
    property bool buddyBusy: false
    property string userText: ""
    property string agentText: ""
    property real level: 0             // your microphone, 0 .. 1
    property real outputLevel: 0       // Buddy's voice, 0 .. 1
    property color speakColor: Theme.accent

    signal endRequested()

    readonly property string caption: {
        if (!active)
            return ""
        if (muted)
            return qsTr("Muted")
        switch (liveState) {
        case "connecting": return qsTr("Connecting…")
        case "thinking": return buddyBusy ? qsTr("Working on it…")
                              : (userText.length > 0 ? userText : qsTr("Thinking…"))
        case "speaking": return agentText.length > 0 ? agentText : qsTr("Speaking…")
        default: return buddyBusy ? qsTr("Working on it…") : qsTr("Listening…")
        }
    }
    readonly property bool speech: active && !muted
                                   && ((liveState === "thinking" && !buddyBusy && userText.length > 0)
                                       || (liveState === "speaking" && agentText.length > 0))
    readonly property bool micOpen: active && !muted && liveState === "listening"

    readonly property int padX: Math.round(14 * s)
    readonly property int dotSize: Math.round(8 * s)
    readonly property int gap: Math.round(10 * s)

    width: Math.min(Math.round(300 * s), label.implicitWidth + padX + dotSize + gap + padX)
    height: Math.max(24, Math.round(32 * s))
    opacity: shown ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    scale: area.pressed ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }

    Glass {
        anchors.fill: parent
        radius: caption.height / 2
        tint: area.containsMouse ? Theme.hover : Theme.surface
        edge: caption.micOpen ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.7) : "transparent"
        Behavior on tint { ColorAnimation { duration: 120 } }
    }

    Rectangle {
        id: dot
        x: caption.padX
        anchors.verticalCenter: parent.verticalCenter
        width: caption.dotSize
        height: width
        radius: width / 2
        color: caption.muted ? Theme.textFaint
             : caption.liveState === "listening" ? Theme.danger
             : caption.liveState === "speaking" ? caption.speakColor : Theme.accent
        // Pulses with the voice: yours while it listens, the agent's while it speaks.
        scale: 1 + 0.5 * (caption.liveState === "speaking" ? caption.outputLevel
                                                            : (caption.muted ? 0 : caption.level))
    }

    Text {
        id: label
        objectName: "captionText"
        x: dot.x + dot.width + caption.gap
        anchors.verticalCenter: parent.verticalCenter
        // Lowercase sits low in the line box, so lift the words onto the dot's centre line.
        anchors.verticalCenterOffset: -1
        width: caption.width - x - caption.padX
        text: caption.caption
        color: caption.speech ? Theme.ink : Theme.textDim
        font.family: Theme.font
        // Not scaled with the orb: at the smallest size the words must still be readable.
        font.pixelSize: Theme.fontMd
        font.weight: caption.speech ? Theme.weightBody : Theme.weightLabel
        elide: caption.speech ? Text.ElideLeft : Text.ElideRight
        maximumLineCount: 1
    }

    MouseArea {
        id: area
        anchors.fill: parent
        objectName: "captionArea"
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("End the conversation")
        onClicked: caption.endRequested()
    }
}
