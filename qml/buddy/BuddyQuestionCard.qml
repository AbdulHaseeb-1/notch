import QtQuick

// Buddy's clarifying question: one to four multiple-choice questions with
// tap-to-answer chips, shown instead of Buddy guessing.
//
//   [chat]  Buddy has a question                                  4:58
//           FORMAT  Which format should the report be?
//           ( PDF )  ( Word )  ( Markdown )  ( Other… )
//                                              [Skip]  [Send]
//
// `request` is BuddyProvider.pendingQuestion. A lone single-choice question
// answers on the tap; with several questions, or a multi-select one, the
// answers are collected and sent with Send. "Other…" opens a line to type
// their own answer. Emits answered(answers) with one { selected: [option
// index], other: "text" } per question, or skipped(). Esc skips; Enter sends
// when every question is answered; 1-4 pick an option of a single question.
Rectangle {
    id: card
    property var request: ({})
    property real maxCardHeight: 380
    signal answered(var answers)
    signal skipped()

    // Held apart from `request`: the provider republishes the question every
    // second for the countdown, and a new array would make the Repeaters rebuild
    // every chip (and lose what is being typed). It changes only with the question.
    property var questions: []
    readonly property string rid: String(request && request.id !== undefined ? request.id : "")
    readonly property int secondsLeft: Number(request ? request.secondsLeft || 0 : 0)
    readonly property bool urgent: secondsLeft > 0 && secondsLeft <= 30
    // A lone single-choice question needs no Send: the tap is the answer.
    readonly property bool tapToAnswer: questions.length === 1 && !questions[0].multiSelect

    // Per question: the chosen option indices, the typed text, whether the
    // "Other…" line is open. Arrays are replaced (not edited) so bindings update.
    property var picks: []
    property var typed: []
    property var typing: []

    readonly property bool complete: {
        if (questions.length === 0)
            return false
        for (let q = 0; q < questions.length; ++q) {
            const has = (picks[q] && picks[q].length > 0)
                        || (typing[q] && String(typed[q] || "").trim().length > 0)
            if (!has)
                return false
        }
        return true
    }

    implicitHeight: Math.min(maxCardHeight, head.height + body.contentHeight + foot.height + 34)
    height: implicitHeight
    radius: Theme.radiusMd
    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.06)
    border.width: 1
    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
    focus: visible
    activeFocusOnTab: true
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Buddy has a question")
    Accessible.description: String(request ? request.title || "" : "")

    onRidChanged: resetState()
    onVisibleChanged: if (visible) forceActiveFocus()
    Component.onCompleted: resetState()

    function resetState() {
        questions = (request && request.questions) ? request.questions : []
        const n = questions.length
        const p = [], t = [], o = []
        for (let i = 0; i < n; ++i) {
            p.push([])
            t.push("")
            o.push(false)
        }
        picks = p
        typed = t
        typing = o
    }

    function isPicked(q, i) {
        return !!(picks[q] && picks[q].indexOf(i) >= 0)
    }

    // Tap an option chip.
    function choose(q, i) {
        const multi = !!questions[q].multiSelect
        const p = picks.slice()
        const cur = (p[q] || []).slice()
        const at = cur.indexOf(i)
        if (multi) {
            if (at >= 0) cur.splice(at, 1)
            else cur.push(i)
        } else {
            cur.length = 0
            cur.push(i)
            // Choosing a chip replaces a typed answer to a single-choice question.
            const o = typing.slice(); o[q] = false; typing = o
        }
        p[q] = cur
        picks = p
        if (tapToAnswer && !multi)
            send()
    }

    function openOther(q) {
        const o = typing.slice()
        o[q] = !o[q]
        typing = o
        if (!questions[q].multiSelect && o[q]) {
            const p = picks.slice(); p[q] = []; picks = p
        }
    }

    function setTyped(q, text) {
        const t = typed.slice()
        t[q] = text
        typed = t
    }

    function send() {
        if (!complete)
            return
        const out = []
        for (let q = 0; q < questions.length; ++q) {
            out.push({
                selected: (picks[q] || []).slice(),
                other: (typing[q] ? String(typed[q] || "").trim() : "")
            })
        }
        answered(out)
    }

    function clock(sec) {
        const s = Math.max(0, sec)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    Keys.onEscapePressed: (event) => { card.skipped(); event.accepted = true }
    Keys.onReturnPressed: (event) => { card.send(); event.accepted = true }
    Keys.onEnterPressed: (event) => { card.send(); event.accepted = true }
    Keys.onPressed: (event) => {
        // 1-4 answer a lone single-choice question from the keyboard.
        if (card.tapToAnswer && event.key >= Qt.Key_1 && event.key <= Qt.Key_4) {
            const i = event.key - Qt.Key_1
            if (i < card.questions[0].options.length) {
                card.choose(0, i)
                event.accepted = true
            }
        }
    }

    component Btn: Rectangle {
        id: b
        property string label
        property bool primary: false
        property bool dim: false
        signal clicked()
        width: lbl.implicitWidth + 24
        height: 28
        radius: Theme.radiusSm + 1
        opacity: dim ? 0.45 : 1
        color: primary ? (ma.pressed ? Theme.stone200 : ma.containsMouse ? Theme.stone50 : Theme.stone100)
                       : (ma.pressed ? Theme.glassFillActive : ma.containsMouse ? Theme.glassFillHover : Theme.glassFill)
        border.width: primary ? 0 : 1
        border.color: Theme.glassBorderStrong
        Accessible.role: Accessible.Button
        Accessible.name: b.label
        scale: ma.pressed && !dim ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Text {
            id: lbl
            anchors.centerIn: parent
            text: b.label
            color: b.primary ? Theme.inverse : Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: !b.dim
            cursorShape: b.dim ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: b.clicked()
        }
    }

    // One tap-to-answer option.
    component Chip: Rectangle {
        id: c
        property string label
        property string hint
        property bool picked: false
        property bool outlined: false   // the "Other…" chip
        signal clicked()
        readonly property real maxW: 300
        width: Math.min(maxW, Math.max(chipLabel.implicitWidth, chipHint.visible ? Math.min(chipHint.implicitWidth, maxW - 28) : 0) + 28 + (picked ? 16 : 0))
        height: col.implicitHeight + 14
        radius: Theme.radiusSm + 3
        color: picked ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                      : (cm.pressed ? Theme.glassFillActive : cm.containsMouse ? Theme.glassFillHover : Theme.glassFill)
        border.width: 0
        border.color: picked ? Theme.accent : Theme.glassBorderStrong
        Accessible.role: Accessible.CheckBox
        Accessible.name: c.label
        Accessible.checked: c.picked
        scale: cm.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Behavior on color { ColorAnimation { duration: 110 } }

        Row {
            anchors.centerIn: parent
            spacing: 6
            Glyph {
                visible: c.picked
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 11
                color: Theme.accent
            }
            Column {
                id: col
                spacing: 1
                Text {
                    id: chipLabel
                    text: c.label
                    color: Theme.ink
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm + 0.5
                    font.weight: Font.DemiBold
                    font.italic: c.outlined
                    textFormat: Text.PlainText
                }
                Text {
                    id: chipHint
                    visible: text.length > 0
                    width: Math.min(implicitWidth, c.maxW - 28)
                    text: c.hint
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fontXs
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }
        MouseArea {
            id: cm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: c.clicked()
        }
    }

    // ---- header ----------------------------------------------------------------
    Item {
        id: head
        x: 12
        y: 11
        width: parent.width - 24
        height: 30

        Rectangle {
            id: mark
            width: 28
            height: 28
            radius: Theme.radiusSm + 2
            anchors.verticalCenter: parent.verticalCenter
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
            Glyph {
                anchors.centerIn: parent
                name: "chat"
                size: 15
                color: Theme.accent
            }
        }
        Text {
            anchors.left: mark.right
            anchors.leftMargin: 10
            anchors.right: clockText.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: card.questions.length > 1 ? qsTr("Buddy has %1 questions").arg(card.questions.length)
                                            : qsTr("Buddy has a question")
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            id: clockText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: card.secondsLeft > 0 ? card.clock(card.secondsLeft) : ""
            color: card.urgent ? Theme.warning : Theme.textFaint
            font.family: Theme.monoFont
            font.pixelSize: Theme.fontXs
        }
    }

    // ---- questions (scrolls when four of them do not fit) ------------------------------
    Flickable {
        id: body
        x: 12
        anchors.top: head.bottom
        anchors.topMargin: 8
        width: parent.width - 24
        height: Math.max(0, Math.min(contentHeight, card.maxCardHeight - head.height - foot.height - 34))
        contentWidth: width
        contentHeight: qs.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: qs
            width: body.width
            spacing: 12

            Repeater {
                model: card.questions
                delegate: Column {
                    id: qCol
                    required property int index
                    required property var modelData
                    width: qs.width
                    spacing: 6

                    Row {
                        width: parent.width
                        spacing: 7
                        Text {
                            visible: String(qCol.modelData.header || "").length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(qCol.modelData.header || "").toUpperCase()
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fontXs - 1
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                            textFormat: Text.PlainText
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - x
                            text: String(qCol.modelData.question || "")
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: qCol.modelData.options || []
                            delegate: Chip {
                                required property int index
                                required property var modelData
                                label: String(modelData.label || "")
                                hint: String(modelData.description || "")
                                picked: card.isPicked(qCol.index, index)
                                onClicked: card.choose(qCol.index, index)
                            }
                        }
                        Chip {
                            label: qsTr("Other…")
                            outlined: true
                            picked: !!card.typing[qCol.index]
                            onClicked: {
                                card.openOther(qCol.index)
                                if (card.typing[qCol.index])
                                    Qt.callLater(function () { other.forceActiveFocus() })
                            }
                        }
                    }

                    // Their own answer.
                    Rectangle {
                        visible: !!card.typing[qCol.index]
                        width: parent.width
                        height: 30
                        radius: Theme.radiusSm
                        color: Theme.field
                        border.width: 1
                        border.color: other.activeFocus ? Theme.accent : Theme.fieldBorder
                        TextInput {
                            id: other
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.ink
                            selectionColor: Theme.accentSoft
                            font.family: Theme.font
                            font.pixelSize: Theme.fontSm + 0.5
                            maximumLength: 500
                            clip: true
                            // Not bound to card.typed: this field is the source of it.
                            onTextChanged: card.setTyped(qCol.index, text)
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("Your own answer")
                            Keys.onReturnPressed: (event) => { card.send(); event.accepted = true }
                            Keys.onEnterPressed: (event) => { card.send(); event.accepted = true }
                            Keys.onEscapePressed: (event) => { card.openOther(qCol.index); card.forceActiveFocus(); event.accepted = true }
                            Text {
                                visible: other.text.length === 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Type your answer")
                                color: Theme.textFaint
                                font: other.font
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- buttons -----------------------------------------------------------------------
    Row {
        id: foot
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 11
        spacing: 6
        height: 28

        Btn { label: qsTr("Skip"); onClicked: card.skipped() }
        Btn {
            label: card.questions.length > 1 ? qsTr("Send answers") : qsTr("Send")
            primary: true
            dim: !card.complete
            visible: !card.tapToAnswer || !!card.typing[0]
            onClicked: card.send()
        }
    }
}
