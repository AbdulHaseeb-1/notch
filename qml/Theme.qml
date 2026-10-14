import QtQuick

// X / Grok palette: true black surfaces, hairline grey borders, white as the
// only accent.
QtObject {
    readonly property color bg: "#000000"
    readonly property color surface: "#0a0b0d"
    readonly property color raised: "#141619"
    readonly property color hover: "#1d2025"
    readonly property color press: "#262a30"
    readonly property color border: "#2b2f33"
    readonly property color borderSoft: "#1c1f23"
    readonly property color rimLight: "#22ffffff"
    readonly property color text: "#edf0f2"
    readonly property color textDim: "#7c8288"
    readonly property color textFaint: "#4d5257"
    readonly property color white: "#ffffff"
    readonly property color danger: "#f4212e"
    readonly property color warn: "#f59e0b"
    readonly property color success: "#10b981"
    readonly property string font: "Segoe UI"
    readonly property string mono: "Cascadia Mono"
}
