import QtQuick

// Colour tokens for the whole app. Dark is the notch's black glass; light is
// the same layout on pale glass. Components read these tokens instead of
// hard-coding colours, so Settings > Light theme switches everything at once.
QtObject {
    readonly property bool light: appearance.light

    // Foreground. "ink" is the high-contrast colour: primary text and accent
    // fills. "inverse" is what sits on top of an ink fill.
    readonly property color ink: light ? "#14171c" : "#ffffff"
    readonly property color inverse: light ? "#ffffff" : "#000000"
    readonly property color inkSoft: light ? "#2c3139" : "#d7dbdc"
    readonly property color text: light ? "#1d2128" : "#edf0f2"
    readonly property color textDim: light ? "#646b76" : "#7c8288"
    readonly property color textFaint: light ? "#9aa1ab" : "#4d5257"

    // Glass and surfaces
    readonly property color surface: light ? "#f7f8fa" : "#0d0f13"
    readonly property color raised: light ? "#eceef2" : "#141619"
    readonly property color hover: light ? "#e1e4ea" : "#1d2025"
    readonly property color press: light ? "#d4d9e0" : "#262a30"
    readonly property color frame: light ? "#e4e7ec" : "#0b0c0f"
    readonly property color track: light ? "#dde1e7" : "#2a2b30"
    readonly property color tile: light ? "#e6e9ee" : "#1a1c20"
    readonly property color tileEdge: light ? "#d3d8df" : "#26292e"
    readonly property color tileActive: light ? "#d6dae1" : "#24262b"

    // Lines and light
    readonly property color border: light ? "#d2d7de" : "#2b2f33"
    readonly property color borderSoft: light ? "#e4e7eb" : "#1c1f23"
    readonly property color borderStrong: light ? "#b9c0ca" : "#3a3d44"
    readonly property color hairline: light ? "#cdffffff" : "#18ffffff"
    readonly property color sheen: light ? "#73ffffff" : "#24ffffff"
    readonly property color shade: light ? "#0f1c2433" : "#14000000"

    // How much the glass tint shows: thinner when the real blur is behind it,
    // denser when it is not (so text stays readable).
    readonly property real glassFrosted: light ? 0.42 : 0.22
    readonly property real glassSolid: light ? 0.86 : 0.74

    // Which monochrome icon variant suits the background.
    readonly property string iconMono: light ? "monoLight" : "mono"

    // Status
    readonly property color danger: light ? "#e11d2a" : "#f4212e"
    readonly property color success: light ? "#059669" : "#10b981"

    readonly property string font: "Segoe UI"
}
