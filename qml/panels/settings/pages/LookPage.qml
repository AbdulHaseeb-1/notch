pragma ComponentBehavior: Bound
import QtQuick

// Settings > Look & glow: the ring round Buddy, the light it throws, how it
// looks while listening and speaking, and the glow round the screen while
// Buddy is in control.
SettingsPage {
    id: page
    required property var panel
    readonly property var prefs: panel.prefs

    title: qsTr("Look & glow")
    subtitle: qsTr("How Buddy's ring and light look, how it shows that it is listening or speaking, and the glow round the screen while it is in control.")

    readonly property var ringStyles: [
        { id: "neon", name: qsTr("Neon") },
        { id: "aurora", name: qsTr("Aurora") },
        { id: "comet", name: qsTr("Comet") },
        { id: "orbit", name: qsTr("Orbit") },
        { id: "dashes", name: qsTr("Beads") },
        { id: "pulse", name: qsTr("Pulse") },
        { id: "prism", name: qsTr("Prism") },
        { id: "halo", name: qsTr("Halo") }
    ]
    readonly property var screenStyles: [
        { id: "soft", name: qsTr("Soft") },
        { id: "prism", name: qsTr("Prism") },
        { id: "pulse", name: qsTr("Pulse") },
        { id: "chase", name: qsTr("Chase") },
        { id: "aurora", name: qsTr("Aurora") }
    ]
    function nameOf(list, id) {
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i].name
        return ""
    }

    // ---- ring style ------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Ring style")
        rightText: page.nameOf(page.ringStyles, page.prefs.buddyRingStyle)
        description: qsTr("The ring of light Buddy wears when it is happy or at work. Hover a style to see it move.")
    }
    SettingsGroupCard {
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.ringStyles
                SettingsOptionTile {
                    id: tile
                    required property var modelData
                    width: Math.floor((parent.width - 3 * 8) / 4)
                    label: modelData.name
                    selected: page.prefs.buddyRingStyle === modelData.id
                    onPicked: {
                        page.prefs.buddyRingStyle = modelData.id
                        page.panel.ringPreviewRequested()
                    }
                    Item {
                        anchors.centerIn: parent
                        width: 76
                        height: 76
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            radius: 14
                            color: Theme.orb
                            Rectangle { x: 8; y: 10; width: 3; height: 6; radius: 1.5; color: "#ffffff" }
                            Rectangle { x: 17; y: 10; width: 3; height: 6; radius: 1.5; color: "#ffffff" }
                        }
                        BuddyRing {
                            anchors.fill: parent
                            style: tile.modelData.id
                            midR: 15.5
                            thickness: 1.8
                            s: 0.55
                            stops: SystemAppearance.ringColors
                            speed: page.prefs.buddyRingSpeed
                            glow: 0.9
                            running: tile.lit && page.visible
                        }
                    }
                }
            }
        }
    }

    SettingsSectionHeader {
        title: qsTr("Ring colour")
        rightText: page.panel.optionLabel(page.panel.ringOpts, SystemAppearance.ring)
    }
    SettingsGroupCard {
        Flow {
            width: parent.width
            spacing: 4
            Repeater {
                model: page.panel.ringOpts
                SettingsRingSwatch {
                    required property var modelData
                    option: modelData
                    selected: SystemAppearance.ring === modelData.id
                    onPicked: {
                        SystemAppearance.ring = modelData.id
                        page.panel.ringPreviewRequested()
                    }
                }
            }
        }

        SettingsCardDivider {}

        SettingsLabeledSlider {
            label: qsTr("Thickness")
            value: (page.prefs.buddyRingThickness - 0.5) / 1.5
            display: Number(page.prefs.buddyRingThickness).toFixed(1) + "x"
            onMoved: (v) => page.prefs.buddyRingThickness = Math.round((0.5 + 1.5 * v) * 10) / 10
        }
        SettingsLabeledSlider {
            label: qsTr("Speed")
            value: (page.prefs.buddyRingSpeed - 0.3) / 2.2
            display: Number(page.prefs.buddyRingSpeed).toFixed(1) + "x"
            onMoved: (v) => page.prefs.buddyRingSpeed = Math.round((0.3 + 2.2 * v) * 10) / 10
        }
        Flow {
            width: parent.width
            spacing: 8
            SettingsPillButton {
                icon: "spark"
                label: qsTr("Preview")
                onClicked: page.panel.ringPreviewRequested()
            }
            SettingsPillButton {
                icon: "reset"
                label: qsTr("Reset ring")
                onClicked: {
                    page.prefs.buddyRingStyle = "neon"
                    page.prefs.buddyRingThickness = 1.0
                    page.prefs.buddyRingSpeed = 1.0
                }
            }
        }
    }

    // ---- glow & shadow --------------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Glow & shadow")
        rightText: ({ bloom: qsTr("Bloom"), floor: qsTr("Floor shadow"), off: qsTr("Off") })[page.prefs.buddyGlowMode] || ""
        description: qsTr("The coloured light Buddy throws, in the colours of its ring.")
    }
    SettingsGroupCard {
        Row {
            width: parent.width
            spacing: 8
            Repeater {
                model: [
                    { id: "bloom", name: qsTr("Bloom") },
                    { id: "floor", name: qsTr("Floor shadow") },
                    { id: "off", name: qsTr("Off") }
                ]
                SettingsOptionTile {
                    id: gt
                    required property var modelData
                    width: Math.floor((parent.width - 2 * 8) / 3)
                    stageHeight: 64
                    label: modelData.name
                    selected: page.prefs.buddyGlowMode === modelData.id
                    onPicked: {
                        page.prefs.buddyGlowMode = modelData.id
                        page.panel.ringPreviewRequested()
                    }
                    Item {
                        anchors.centerIn: parent
                        width: 76
                        height: 64
                        visible: gt.modelData.id !== "off"
                        BuddyGlow {
                            x: parent.width / 2 - width / 2
                            y: parent.height / 2 - height / 2 - (gt.modelData.id === "floor" ? 6 : 0)
                            width: reach * 2
                            height: width
                            orbRadius: 11
                            s: 0.34
                            stops: SystemAppearance.ringColors
                            mode: gt.modelData.id
                            strength: 1
                            activity: 1
                            running: gt.lit && page.visible
                            scale: 0.9
                        }
                        Rectangle {
                            x: parent.width / 2 - 11
                            y: parent.height / 2 - 11 - (gt.modelData.id === "floor" ? 6 : 0)
                            width: 22
                            height: 22
                            radius: 11
                            color: Theme.orb
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        visible: gt.modelData.id === "off"
                        width: 22
                        height: 22
                        radius: 11
                        color: Theme.orb
                        border.width: 1
                        border.color: Theme.borderStrong
                    }
                }
            }
        }

        SettingsLabeledSlider {
            visible: page.prefs.buddyGlowMode !== "off"
            label: qsTr("Strength")
            value: page.prefs.buddyGlowStrength
            display: Math.round(page.prefs.buddyGlowStrength * 100) + "%"
            onMoved: (v) => page.prefs.buddyGlowStrength = Math.round(v * 100) / 100
        }
        SettingsToggleRow {
            visible: page.prefs.buddyGlowMode !== "off"
            label: qsTr("Keep a faint glow when idle")
            sub: qsTr("Otherwise the light only shows while Buddy is happy, working, listening or speaking.")
            checked: page.prefs.buddyGlowAlways
            onToggled: (c) => page.prefs.buddyGlowAlways = c
        }
    }

    // ---- listening & speaking -----------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Listening & speaking")
        rightText: ({ off: qsTr("Off"), rings: qsTr("Rings"), wave: qsTr("Waves"), full: qsTr("Full") })[page.prefs.buddyVoiceFx] || ""
        description: qsTr("While the microphone is open a ribbon of waves follows your voice and rings of sound draw in. While Buddy speaks, a crown of bars dances and rings go out.")
    }
    SettingsGroupCard {
        Segmented {
            width: parent.width
            height: 34
            options: [
                { label: qsTr("Off"), value: "off" },
                { label: qsTr("Rings"), value: "rings" },
                { label: qsTr("Waves"), value: "wave" },
                { label: qsTr("Full"), value: "full" }
            ]
            value: page.prefs.buddyVoiceFx
            onPicked: (v) => page.prefs.buddyVoiceFx = v
        }
        Flow {
            width: parent.width
            spacing: 8
            SettingsPillButton {
                icon: "mic"
                label: qsTr("Preview listening")
                enabled: page.prefs.buddyVoiceFx !== "off"
                onClicked: page.panel.fxPreviewRequested("listen")
            }
            SettingsPillButton {
                icon: "volume"
                label: qsTr("Preview speaking")
                enabled: page.prefs.buddyVoiceFx !== "off"
                onClicked: page.panel.fxPreviewRequested("speak")
            }
        }
    }

    // ---- the glow round the screen ------------------------------------------------------------------
    SettingsSectionHeader {
        title: qsTr("Screen glow")
        rightText: page.prefs.buddyCuShowGlow ? page.nameOf(page.screenStyles, page.prefs.buddyCuGlowStyle) : qsTr("Off")
        description: qsTr("Shown on every screen while Buddy controls the computer, so it is always clear who is driving. It never takes clicks or focus.")
    }
    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Glow round the screen while in control")
            checked: page.prefs.buddyCuShowGlow
            onToggled: (c) => page.prefs.buddyCuShowGlow = c
        }
        Flow {
            visible: page.prefs.buddyCuShowGlow
            width: parent.width
            spacing: 8
            Repeater {
                model: page.screenStyles
                SettingsOptionTile {
                    id: st
                    required property var modelData
                    width: Math.floor((parent.width - 4 * 8) / 5)
                    stageHeight: 54
                    label: modelData.name
                    selected: page.prefs.buddyCuGlowStyle === modelData.id
                    onPicked: page.prefs.buddyCuGlowStyle = modelData.id
                    // The screen edge in miniature.
                    Item {
                        id: mini
                        anchors.centerIn: parent
                        width: Math.min(76, parent.width - 16)
                        height: 44
                        readonly property bool coloured: st.modelData.id === "prism" || st.modelData.id === "aurora"
                        readonly property var pal: SystemAppearance.ringColors
                        readonly property color cTop: coloured ? Qt.color(pal[0]) : Theme.warning
                        readonly property color cRight: coloured ? Qt.color(pal[1]) : Theme.warning
                        readonly property color cBottom: coloured ? Qt.color(pal[2]) : Theme.warning
                        readonly property color cLeft: coloured ? Qt.color(pal[3]) : Theme.warning
                        readonly property real k: st.modelData.id === "chase" ? 0.45 : (st.modelData.id === "pulse" ? 1 : 0.8)
                        Rectangle { anchors.fill: parent; radius: 5; color: Theme.stone900; border.width: 1; border.color: Theme.borderStrong }
                        Rectangle {
                            width: parent.width; height: 12; radius: 5
                            gradient: Gradient {
                                GradientStop { position: 0; color: Qt.rgba(mini.cTop.r, mini.cTop.g, mini.cTop.b, 0.85 * mini.k) }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                        Rectangle {
                            y: parent.height - height
                            width: parent.width; height: 12; radius: 5
                            gradient: Gradient {
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: Qt.rgba(mini.cBottom.r, mini.cBottom.g, mini.cBottom.b, 0.85 * mini.k) }
                            }
                        }
                        Rectangle {
                            width: 12; height: parent.height; radius: 5
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Qt.rgba(mini.cLeft.r, mini.cLeft.g, mini.cLeft.b, 0.85 * mini.k) }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                        Rectangle {
                            x: parent.width - width
                            width: 12; height: parent.height; radius: 5
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: Qt.rgba(mini.cRight.r, mini.cRight.g, mini.cRight.b, 0.85 * mini.k) }
                            }
                        }
                    }
                }
            }
        }
        SettingsLabeledSlider {
            visible: page.prefs.buddyCuShowGlow
            label: qsTr("Intensity")
            value: page.prefs.buddyCuGlowIntensity
            display: Math.round(page.prefs.buddyCuGlowIntensity * 100) + "%"
            onMoved: (v) => page.prefs.buddyCuGlowIntensity = Math.max(0.1, Math.round(v * 100) / 100)
        }
        SettingsCardDivider {}
        SettingsToggleRow {
            label: qsTr("Show Buddy's cursor")
            sub: qsTr("Draw Buddy's own pointer gliding to each target, with click ripples, drag trails and a typing indicator. It never takes focus or blocks your own pointer.")
            checked: page.prefs.buddyCuShowCursor
            onToggled: (c) => page.prefs.buddyCuShowCursor = c
        }
    }
}
