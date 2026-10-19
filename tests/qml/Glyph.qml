import QtQuick

// Test stand-in for qml/Glyph.qml (the real one draws SVG paths through the
// IconSet C++ singleton). Components under test only read these properties.
Item {
    property string name
    property real size: 16
    property real stroke: 1.6
    property color color: "white"
    width: size
    height: size
}
