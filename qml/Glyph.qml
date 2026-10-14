import QtQuick
import QtQuick.Shapes

// Vector glyph icons on a 24-unit grid with support for stroked and filled icons.
Item {
    id: glyph
    property string name: "close"
    property color color: "#e7e9ea"
    property real size: 16
    property real stroke: 1.8
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    // Glyphs drawn as solid shapes; everything else is a line icon.
    readonly property var fillIcons: ({
        "play": true,
        "pause": true,
        "prev": true,
        "next": true,
        "more": true,
        "note": true,
        "notes": true
    })

    readonly property bool isFill: filled || !!fillIcons[glyph.name]

    readonly property var paths: ({
        "close": "M6 6 L18 18 M18 6 L6 18",
        "plus": "M12 5 L12 19 M5 12 L19 12",
        "open": "M7.5 16.5 L16.5 7.5 M9 7.5 L16.5 7.5 L16.5 15",
        "folder": "M3.5 7.5 Q3.5 5.5 5.5 5.5 L9.5 5.5 L11.5 7.5 L18.5 7.5 Q20.5 7.5 20.5 9.5 L20.5 16.5 Q20.5 18.5 18.5 18.5 L5.5 18.5 Q3.5 18.5 3.5 16.5 Z",
        "unpin": "M4.5 7 L19.5 7 M9.5 7 L9.5 4.5 L14.5 4.5 L14.5 7 M6.5 7 L7.5 19.5 L16.5 19.5 L17.5 7",
        "left": "M14.5 6 L8.5 12 L14.5 18",
        "right": "M9.5 6 L15.5 12 L9.5 18",
        "power": "M12 3.5 L12 11 M7.2 6.4 A7.5 7.5 0 1 0 16.8 6.4",
        "reset": "M4.5 12 A7.5 7.5 0 1 0 7 6.4 M4.5 3.5 L4.5 8 L9 8",
        "check": "M5 12.5 L10 17.5 L19 7",
        "settings": "M4 7 H13 M17 7 H20 M4 17 H7 M11 17 H20 M15 4.5 V9.5 M9 14.5 V19.5",
        "list": "M4 6.5 H20 M4 12 H20 M4 17.5 H13",

        // ---- transport (solid, softly rounded corners)
        "play": "M8 5.6 C8 4.8 8.9 4.3 9.6 4.8 L19 11.2 C19.6 11.6 19.6 12.4 19 12.8 L9.6 19.2 C8.9 19.7 8 19.2 8 18.4 Z",
        "pause": "M7.5 5 H9.5 A1 1 0 0 1 10.5 6 V18 A1 1 0 0 1 9.5 19 H7.5 A1 1 0 0 1 6.5 18 V6 A1 1 0 0 1 7.5 5 Z M14.5 5 H16.5 A1 1 0 0 1 17.5 6 V18 A1 1 0 0 1 16.5 19 H14.5 A1 1 0 0 1 13.5 18 V6 A1 1 0 0 1 14.5 5 Z",
        "prev": "M6 5 H7 A1 1 0 0 1 8 6 V18 A1 1 0 0 1 7 19 H6 A1 1 0 0 1 5 18 V6 A1 1 0 0 1 6 5 Z M19 6.1 V17.9 C19 18.7 18.1 19.2 17.4 18.7 L10.6 13 C10 12.5 10 11.5 10.6 11 L17.4 5.3 C18.1 4.8 19 5.3 19 6.1 Z",
        "next": "M17 5 H18 A1 1 0 0 1 19 6 V18 A1 1 0 0 1 18 19 H17 A1 1 0 0 1 16 18 V6 A1 1 0 0 1 17 5 Z M5 6.1 C5 5.3 5.9 4.8 6.6 5.3 L13.4 11 C14 11.5 14 12.5 13.4 13 L6.6 18.7 C5.9 19.2 5 18.7 5 17.9 Z",

        // ---- music notes (solid)
        "note": "M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z",
        "notes": "M9 5 L20 2.5 L20 5.5 L9 8 Z M9 5 H11 V17 H9 Z M18 3 H20 V14.5 H18 Z M5 17 A3 3 0 1 1 11 17 A3 3 0 1 1 5 17 Z M14 14.5 A3 3 0 1 1 20 14.5 A3 3 0 1 1 14 14.5 Z",

        // ---- player line icons
        "heart": "M12 19.5 C12 19.5 3.5 14.6 3.5 8.9 C3.5 6.4 5.4 4.5 7.8 4.5 C9.6 4.5 11.1 5.6 12 7 C12.9 5.6 14.4 4.5 16.2 4.5 C18.6 4.5 20.5 6.4 20.5 8.9 C20.5 14.6 12 19.5 12 19.5 Z",
        "volume": "M4 9.5 H7 L11.5 5.5 V18.5 L7 14.5 H4 Z M15 9.2 C16.4 10.6 16.4 13.4 15 14.8 M17.8 6.5 C20.8 9.5 20.8 14.5 17.8 17.5",
        "mute": "M4 9.5 H7 L11.5 5.5 V18.5 L7 14.5 H4 Z M15.5 9.5 L20.5 14.5 M20.5 9.5 L15.5 14.5",
        "shuffle": "M3.5 7 H6.8 C10.5 7 13.5 17 17.2 17 H20.5 M17.5 14 L20.5 17 L17.5 20 M3.5 17 H6.8 C8.3 17 9.6 15.4 10.7 13.5 M13.3 10.5 C14.4 8.6 15.7 7 17.2 7 H20.5 M17.5 4 L20.5 7 L17.5 10",
        "repeat": "M17 3 L20 6 L17 9 M4 11 V10 C4 7.8 5.8 6 8 6 H20 M7 21 L4 18 L7 15 M20 13 V14 C20 16.2 18.2 18 16 18 H4",
        "chevron-down": "M6.5 9.5 L12 15 L17.5 9.5",
        "more": "M6 10c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm12 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm-6 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z",

        "edit": "M15.5 5.5 L18.5 8.5 M5 19 L8.5 18.2 L18.5 8.2 Q19.2 7.5 18.5 6.8 L17.2 5.5 Q16.5 4.8 15.8 5.5 L5.8 15.5 Z",
        "activity": "M3.5 12 L7.5 12 L10 5.5 L14 18.5 L16.5 12 L20.5 12"
    })

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: glyph.width / 24; yScale: glyph.height / 24 }

        ShapePath {
            strokeColor: glyph.isFill ? "transparent" : glyph.color
            strokeWidth: glyph.isFill ? 0 : glyph.stroke * 24 / Math.max(1, glyph.width)
            fillColor: glyph.isFill ? glyph.color : "transparent"
            fillRule: ShapePath.WindingFill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: glyph.paths[glyph.name] || "" }
        }
    }
}
