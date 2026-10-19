import QtQuick

// Status mark for one agent tool step: a slowly turning loader while it runs,
// otherwise a tinted check / cross / shield. The spinner uses a
// RotationAnimator (render thread, no bindings re-evaluated per frame) and
// only runs while the step is running and the item is visible.
Item {
    id: si
    property string status: "done"
    property real size: 14
    width: size
    height: size

    readonly property bool running: status === "running"

    Glyph {
        id: spin
        anchors.centerIn: parent
        visible: si.running
        name: "loader"
        size: si.size
        stroke: 1.7
        color: Theme.running
        RotationAnimator on rotation {
            running: si.running && si.visible
            from: 0; to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }

    Glyph {
        anchors.centerIn: parent
        visible: !si.running
        name: Theme.statusIcon(si.status)
        size: si.size
        stroke: 1.6
        color: Theme.statusColor(si.status)
    }
}
