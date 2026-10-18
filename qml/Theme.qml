import QtQuick

// Colour and motion tokens for the whole app.
// Clean, opaque surfaces with zero glass/frosted translucency. The surface
// family (carbon, midnight, graphite, mocha) and the accent are chosen in
// Settings; Appearance resolves them for light/dark into one shared map that
// every Theme instance reads, so they all change together.
QtObject {
    readonly property bool light: appearance.light
    readonly property var c: appearance.colors

    // Foreground. "ink" is the high-contrast colour: primary text, icons and
    // anything that has to read at a glance. "inverse" is what sits on top of
    // an accent fill (a selected segment, a primary button).
    readonly property color ink: light ? "#18181b" : "#f0f0f2"
    readonly property color inverse: light ? "#ffffff" : "#121214"
    readonly property color inkSoft: light ? "#3f3f46" : "#bdbdc4"
    readonly property color text: light ? "#202024" : "#dcdce0"
    readonly property color textDim: light ? "#65656f" : "#90909a"
    readonly property color textFaint: light ? "#9494a0" : "#63636e"

    // Opaque surface tones (no glass transparency).
    readonly property color surface: c.surface
    readonly property color content: c.content

    // Solid elevated surfaces and interactive states.
    readonly property color raised: c.raised
    readonly property color hover: c.hover
    readonly property color press: c.press
    readonly property color tile: c.tile
    readonly property color tileEdge: c.tileEdge
    readonly property color tileActive: c.tileActive
    readonly property color track: c.track
    readonly property color frame: c.frame

    // Text inputs sit slightly deeper than a card, with a crisp border.
    readonly property color field: c.field
    readonly property color fieldBorder: c.fieldBorder

    // Lines & borders
    readonly property color border: c.border
    readonly property color borderSoft: c.borderSoft
    readonly property color borderStrong: c.borderStrong
    readonly property color hairline: light ? "#cc000000" : "#cce4e4e4"

    // The desktop buddy's body (always dark; tinted to match the family).
    readonly property color orb: c.orb

    // Surface rim tokens kept for compatibility, flat and non-glossy.
    readonly property color rimTop: border
    readonly property color rimMid: border
    readonly property color rimBottom: border
    readonly property color sheen: "transparent"
    readonly property color shadow: "transparent"

    // Fully opaque surfaces (glass effect removed).
    readonly property real glassFrosted: 1.0
    readonly property real glassSolid: 1.0

    // Which monochrome icon variant suits the background.
    readonly property string iconMono: light ? "monoLight" : "mono"

    // Status colours.
    readonly property color danger: light ? "#cf2f52" : "#e34671"
    readonly property color dangerSoft: light ? "#26cf2f52" : "#26e34671"
    readonly property color warning: light ? "#a86b21" : "#f1b467"
    readonly property color success: light ? "#2f7d4f" : "#3fa266"

    // Accents: links, primary buttons, focus rings, badges.
    readonly property color accent: c.accent
    readonly property color accentHover: c.accentHover
    readonly property color badge: light ? "#4c8492" : "#88c0d0"

    readonly property string font: "Segoe UI"
}

