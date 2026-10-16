import QtQuick

// The buddy's request pill: the compact text field that pops out beside the
// mascot once it is clicked. Same glass capsule styling as the rest of the
// notch, with a status row underneath for processing / error messages.
Item {
    id: bar

    property string status: "idle"   // idle | thinking | error
    property string statusText: ""
    property string placeholder: qsTr("What should I help with?")
    signal submitted(string text)
    signal dismissed()
    signal retryRequested()

    function focusInput() {
        field.forceActiveFocus()
        field.selectAll()
    }

    readonly property color accent: t.accent

    implicitWidth: 336
    implicitHeight: statusText.length > 0 ? 64 : 42
    width: implicitWidth
    height: implicitHeight

    Behavior on implicitHeight { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

    Theme { id: t }

    Glass {
        anchors.fill: parent
        radius: 21
    }

    Column {
        anchors.fill: parent
        spacing: 4

        // ---- field row ----------------------------------------------------
        Item {
            width: parent.width
            height: 42

            Glyph {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                name: "spark"
                size: 15
                color: field.activeFocus ? bar.accent : t.textFaint
            }

            TextInput {
                id: field
                anchors.left: parent.left
                anchors.leftMargin: 36
                anchors.right: sendBtn.visible ? sendBtn.left : xBtn.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                color: t.ink
                selectionColor: bar.accent
                selectedTextColor: t.surface
                font.family: t.font
                font.pixelSize: 13
                font.weight: Font.Medium
                clip: true
                selectByMouse: true
                enabled: bar.status !== "thinking"

                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Ask Noch's buddy")
                Accessible.description: qsTr("Type a request, then press Enter")

                Keys.onReturnPressed: (event) => { bar.trySubmit(); event.accepted = true }
                Keys.onEnterPressed: (event) => { bar.trySubmit(); event.accepted = true }
                Keys.onEscapePressed: (event) => { bar.dismissed(); event.accepted = true }

                Text {
                    anchors.fill: parent
                    visible: field.text.length === 0
                    verticalAlignment: TextInput.AlignVCenter
                    text: bar.placeholder
                    color: t.textFaint
                    font: field.font
                    elide: Text.ElideRight
                }
            }

            // Send / spinner. Disabled while thinking so a double Enter or a
            // second click cannot fire the same request twice.
            Item {
                id: sendBtn
                visible: field.text.length > 0 || bar.status === "thinking"
                anchors.right: xBtn.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26
                opacity: bar.status === "thinking" ? 1 : (sendHit.containsMouse ? 1 : 0.72)

                // Three dots with a travelling pulse while the handler runs.
                Row {
                    anchors.centerIn: parent
                    spacing: 3
                    visible: bar.status === "thinking"

                    Repeater {
                        model: [0, 120, 240]

                        Rectangle {
                            id: dot
                            required property int modelData
                            width: 4
                            height: 4
                            radius: 2
                            color: bar.accent
                            SequentialAnimation on opacity {
                                running: bar.status === "thinking"
                                loops: Animation.Infinite
                                PauseAnimation { duration: dot.modelData }
                                NumberAnimation { to: 0.25; duration: 360 }
                                NumberAnimation { to: 1.0; duration: 360 }
                            }
                        }
                    }
                }

                Glyph {
                    anchors.centerIn: parent
                    visible: bar.status !== "thinking"
                    name: "send"
                    size: 15
                    stroke: 2
                    color: sendHit.containsMouse ? t.ink : t.textDim
                }
                MouseArea {
                    id: sendHit
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: bar.status !== "thinking"
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Send request")
                    onClicked: bar.trySubmit()
                }
            }

            // Dismiss.
            Item {
                id: xBtn
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: bar.status !== "thinking"
                width: 22
                height: 22

                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 12
                    stroke: 2
                    color: xHit.containsMouse ? t.ink : t.textFaint
                }
                MouseArea {
                    id: xHit
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Dismiss request")
                    onClicked: { field.text = ""; bar.dismissed() }
                }
            }
        }

        // ---- status / error row -------------------------------------------
        Item {
            width: parent.width - 28
            x: 14
            height: bar.statusText.length > 0 ? 16 : 0
            clip: true

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - retryLabel.width - 6
                text: bar.statusText
                color: bar.status === "error" ? t.danger : t.textDim
                font.family: t.font
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Text {
                id: retryLabel
                visible: bar.status === "error"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Retry")
                color: retryHit.containsMouse ? t.ink : t.accent
                font.family: t.font
                font.pixelSize: 11
                font.weight: Font.DemiBold

                MouseArea {
                    id: retryHit
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.retryRequested()
                }
            }
        }
    }

    function trySubmit() {
        const text = field.text.trim()
        if (text.length === 0 || bar.status === "thinking")
            return
        bar.submitted(text)
        field.selectAll()
    }
}
