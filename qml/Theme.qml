import QtQuick

// Colour and motion tokens for the whole app. Dark is the notch's black glass;
// light is the same layout on pale glass. Components read these tokens instead
// of hard-coding colours, so Settings > Light theme switches everything at once.
//
// Controls sit *on* the glass, so their fills are translucent white (dark) or
// translucent black (light) rather than fixed greys: they pick up whatever the
// surface is and stay consistent in both themes, the way Apple's materials do.
QtObject {
    readonly property bool light: appearance.light

    // Foreground. "ink" is the high-contrast colour: primary text and accent
    // fills. "inverse" is what sits on top of an ink fill.
    readonly property color ink: light ? "#1d1d1f" : "#ffffff"
    readonly property color inverse: light ? "#ffffff" : "#000000"
    readonly property color inkSoft: light ? "#3a3a3d" : "#e3e3e6"
    readonly property color text: light ? "#1d1d1f" : "#f2f2f4"
    readonly property color textDim: light ? "#6e6e73" : "#98989f"
    readonly property color textFaint: light ? "#a1a1a6" : "#636368"

    // Glass body. Opaque enough to read on any wallpaper without the blur.
    readonly property color surface: light ? "#f6f6f8" : "#0c0c0e"

    // Control fills (translucent, drawn over the glass)
    readonly property color raised: light ? "#0d000000" : "#0fffffff"
    readonly property color hover: "transparent"
    readonly property color press: light ? "#1f000000" : "#26ffffff"
    readonly property color tile: light ? "#0d000000" : "#14ffffff"
    readonly property color tileEdge: light ? "#14000000" : "#1fffffff"
    readonly property color tileActive: light ? "#1a000000" : "#24ffffff"
    readonly property color track: light ? "#1a000000" : "#29ffffff"
    readonly property color frame: light ? "#e6e6ea" : "#0a0a0c"

    // Lines
    readonly property color border: light ? "#1f000000" : "#1fffffff"
    readonly property color borderSoft: light ? "#0f000000" : "#12ffffff"
    readonly property color borderStrong: light ? "#38000000" : "#40ffffff"
    readonly property color hairline: light ? "#cdffffff" : "#2effffff"

    // Glass rim: a 1px ring that is lit from above. Brightest at the top edge,
    // faint down the sides, a touch of light returning along the bottom.
    readonly property color rimTop: light ? "#f2ffffff" : "#52ffffff"
    readonly property color rimMid: light ? "#26000000" : "#14ffffff"
    readonly property color rimBottom: light ? "#1a000000" : "#24ffffff"
    // Light falling across the upper part of the surface.
    readonly property color sheen: light ? "#80ffffff" : "#0dffffff"
    readonly property color shadow: "transparent"

    // Glass opacity: thinner when the real blur is behind it, denser when it
    // is not (so text stays readable on any wallpaper).
    readonly property real glassFrosted: light ? 0.62 : 0.58
    readonly property real glassSolid: light ? 0.94 : 0.92

    // Which monochrome icon variant suits the background.
    readonly property string iconMono: light ? "monoLight" : "mono"

    // Status
    readonly property color danger: light ? "#e11d2a" : "#ff453a"
    readonly property color dangerSoft: light ? "#1fe11d2a" : "#26ff453a"
    readonly property color success: light ? "#1f9d55" : "#32d74b"
    readonly property color accent: light ? "#0a84ff" : "#0a84ff"

    readonly property string font: "Segoe UI"
}
