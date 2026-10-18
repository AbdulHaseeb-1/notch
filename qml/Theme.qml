pragma Singleton
import QtQuick

// One theme for the whole app: dark stone.
//
// Warm, neutral dark greys with subtle borders and restrained contrast. There
// is no light mode, no surface family and no accent picker any more: every
// colour, type size, spacing step, radius and panel width lives here, once,
// and every component reads this singleton (`Theme.ink`, `Theme.sp3`, ...).
//
// Everything is a plain readonly constant, so reading a token costs nothing
// at run time and there is nothing to re-evaluate when "the theme changes".
QtObject {
    // ---- stone ramp (darkest -> lightest) -------------------------------------
    readonly property color stone950: "#0f0e0d"
    readonly property color stone900: "#151413"
    readonly property color stone850: "#1a1918"
    readonly property color stone800: "#1f1e1c"
    readonly property color stone750: "#252321"
    readonly property color stone700: "#2c2a27"
    readonly property color stone650: "#34312e"
    readonly property color stone600: "#3d3a36"
    readonly property color stone500: "#57534d"
    readonly property color stone400: "#78736b"
    readonly property color stone300: "#a29c93"
    readonly property color stone200: "#cdc7be"
    readonly property color stone100: "#e7e3dc"
    readonly property color stone50:  "#f4f1ec"

    // ---- foreground ----------------------------------------------------------
    // "ink" is the high-contrast colour (primary text, icons that must read at
    // a glance); "inverse" sits on top of a light fill (a selected segment, a
    // primary button).
    readonly property color ink: stone50
    readonly property color inkSoft: stone200
    readonly property color text: stone100
    readonly property color textDim: stone300
    readonly property color textFaint: stone400
    readonly property color inverse: stone900

    // ---- surfaces ------------------------------------------------------------
    readonly property color surface: stone850        // notch, flyouts, cards
    readonly property color content: stone800
    readonly property color raised: stone800         // group cards inside a flyout
    readonly property color hover: stone700
    readonly property color press: stone650
    readonly property color tile: stone750           // chips, small buttons
    readonly property color tileEdge: stone600
    readonly property color tileActive: stone700
    readonly property color track: stone650          // slider / toggle tracks
    readonly property color frame: stone750
    readonly property color field: stone900          // text inputs sit deeper
    readonly property color fieldBorder: stone650
    readonly property color orb: stone950            // the desktop buddy's body

    // ---- lines ---------------------------------------------------------------
    readonly property color border: "#33302d"
    readonly property color borderSoft: "#292725"
    readonly property color borderStrong: stone600
    readonly property color hairline: "#26ffffff"

    // ---- glass ---------------------------------------------------------------
    // Semi-transparent layers for the tool-call trace and history lists: a
    // whisper of light over the stone surface plus a faint rim. No blur and
    // no effects, so they are as cheap as a plain Rectangle.
    readonly property real surfaceAlpha: 0.95        // flyouts / panels over the desktop
    readonly property color glassFill: "#0affffff"     // ~4 % white
    readonly property color glassFillHover: "#12ffffff"
    readonly property color glassFillActive: "#1affffff"
    readonly property color glassBorder: "#14ffffff"
    readonly property color glassBorderStrong: "#24ffffff"
    readonly property color glassDeep: "#59000000"     // recessed console wells
    readonly property color scrim: "#8c000000"

    // ---- accent & status -------------------------------------------------------
    // The accent is a pale warm stone, not a hue: focus rings, selected
    // controls and the primary button read as "lit" without adding colour.
    readonly property color accent: "#d9d2c7"
    readonly property color accentHover: "#ebe6dd"
    readonly property color accentSoft: "#1fd9d2c7"
    readonly property color badge: stone200

    readonly property color danger: "#e07a6e"
    readonly property color dangerSoft: "#24e07a6e"
    readonly property color warning: "#d8b46e"
    readonly property color success: "#9cbf8e"
    readonly property color successSoft: "#1f9cbf8e"
    readonly property color running: "#d9d2c7"       // live tool steps: lit stone

    // The buddy's halo: four stops around the ring, in stone and warm silver.
    readonly property var ringColors: ["#efe9df", "#9b938a", "#fbf7f0", "#6f6860"]

    // Which monochrome app-icon variant suits the (always dark) background.
    readonly property string iconMono: "mono"

    // ---- typography ----------------------------------------------------------
    readonly property string font: "Segoe UI"
    readonly property string monoFont: "Consolas"
    readonly property int fontXs: 10
    readonly property int fontSm: 11
    readonly property int fontBase: 12
    readonly property int fontMd: 13
    readonly property int fontLg: 15
    readonly property int fontXl: 17

    // ---- spacing, radii, sizes -------------------------------------------------
    readonly property int sp1: 4
    readonly property int sp2: 8
    readonly property int sp3: 12
    readonly property int sp4: 16
    readonly property int sp5: 20
    readonly property int sp6: 24

    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusXl: 20

    // Icons: one stroke weight everywhere (device-independent px), a few sizes.
    readonly property real iconStroke: 1.6
    readonly property int iconSm: 12
    readonly property int iconMd: 15
    readonly property int iconLg: 18

    // Panel widths: generous, but always clamped to the screen by the caller.
    readonly property int screenMargin: 24
    readonly property int settingsWidth: 880
    readonly property int hubWidth: 460
    readonly property int paletteWidth: 600
    readonly property int musicWidth: 400
    readonly property int buddyPanelWidth: 540

    // Clamp a preferred width into the available screen width.
    function fit(preferred, available) {
        return Math.round(Math.max(280, Math.min(preferred, available - 2 * screenMargin)))
    }
}
