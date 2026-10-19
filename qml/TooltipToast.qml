import QtQuick

// Tooltip / toast / size HUD bubble that floats under the notch.
Rectangle {
    id: self

    property var root: null
    property var cfg: null


    readonly property bool showHud: root.hudVisible
    readonly property string label: showHud ? Math.round(cfg.sizeScale * 100) + "%" : root.tipText

    readonly property real targetWidth: Math.min(root.width - 24, tipLabel.implicitWidth + 20)
    readonly property real targetX: Math.round(Math.max(12, Math.min(root.width - targetWidth - 12,
           (showHud || root.toast.length > 0 ? root.width / 2 : root.tipX) - targetWidth / 2)))

    width: targetWidth
    height: 26
    radius: 13
    color: showHud ? Theme.surface : Theme.accent
    border.width: showHud ? 1 : 0
    border.color: Theme.borderStrong
    x: targetX
    y: root.shownY + root.pillH + 12 + (shown ? 0 : -4)
    opacity: shown ? 1 : 0
    scale: shown ? 1 : 0.94
    readonly property bool shown: showHud || root.tipVisible

    Behavior on opacity { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    Behavior on x { enabled: self.opacity > 0.5; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Text {
        id: tipLabel
        anchors.centerIn: parent
        width: Math.min(implicitWidth, self.width - 20)
        elide: Text.ElideRight
        text: self.label
        color: self.showHud ? Theme.text : Theme.inverse
        font.family: Theme.font
        font.pixelSize: 12
        font.weight: Font.Bold
    }
}