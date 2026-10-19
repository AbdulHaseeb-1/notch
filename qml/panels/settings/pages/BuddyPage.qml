pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Buddy")
    subtitle: qsTr("The desktop companion: show it, give it sound, and switch on real AI chat.")

    SettingsSectionHeader {
        title: qsTr("Desktop buddy")
        rightText: panel.prefs.buddyEnabled
                     ? (panel.realChat ? panel.labelFor(panel.prefs.buddyProvider) : qsTr("Local mode"))
                     : qsTr("Disabled")
    }

    SettingsGroupCard {
        Item {
            width: parent.width
            implicitHeight: enableBuddyRow.implicitHeight

            SettingsToggleRow {
                id: enableBuddyRow
                width: parent.width - (resetBuddyBtn2.visible ? resetBuddyBtn2.width + 12 : 0)
                label: qsTr("Enable desktop buddy")
                sub: qsTr("Show the interactive orb on your desktop. Drag to move, click to ask.")
                checked: panel.prefs.buddyEnabled
                onToggled: (c) => panel.prefs.buddyEnabled = c
            }

            SettingsPillButton {
                id: resetBuddyBtn2
                visible: panel.buddyMovedCustom
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                icon: "reset"
                label: qsTr("Reset position")
                onClicked: panel.resetBuddyPosRequested()
            }
        }

        SettingsCardDivider {}

        Item {
            width: parent.width
            implicitHeight: sfxToggleRow.implicitHeight

            SettingsToggleRow {
                id: sfxToggleRow
                width: parent.width - (previewSfxBtn.width + 12)
                label: qsTr("Buddy sound effects")
                sub: qsTr("Expressive companion chirps, purrs, and chimes when interacting.")
                checked: panel.prefs.buddySfxEnabled
                onToggled: (c) => {
                    panel.prefs.buddySfxEnabled = c
                    if (c && BuddySound)
                        Qt.callLater(function() { BuddySound.play("wake") })
                }
            }

            SettingsPillButton {
                id: previewSfxBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                icon: panel.prefs.buddySfxEnabled ? "volume" : "mute"
                label: qsTr("Test sound")
                enabled: panel.prefs.buddySfxEnabled
                onClicked: {
                    if (BuddySound)
                        BuddySound.play("happy")
                }
            }
        }

        Row {
            visible: panel.prefs.buddySfxEnabled
            width: parent.width
            spacing: 14

            Column {
                width: Math.floor((parent.width - 14) * 0.54)
                spacing: 6

                Text {
                    width: parent.width
                    text: qsTr("Voice style")
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                }

                Segmented {
                    width: parent.width
                    height: 30
                    options: [
                        { label: qsTr("Cute"), value: "cute" },
                        { label: qsTr("Warm"), value: "warm" },
                        { label: qsTr("Mellow"), value: "mellow" }
                    ]
                    value: panel.prefs.buddySfxStyle
                    onPicked: (v) => {
                        panel.prefs.buddySfxStyle = v
                        if (BuddySound)
                            Qt.callLater(function() { BuddySound.play("wake") })
                    }
                }
            }

            Column {
                width: parent.width - Math.floor((parent.width - 14) * 0.54) - 14
                spacing: 6

                Row {
                    width: parent.width
                    Text {
                        text: qsTr("Sound volume")
                        color: Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                    }
                    Item { width: Math.max(4, parent.width - 115); height: 1 }
                    Text {
                        text: Math.round(panel.prefs.buddySfxVolume * 100) + "%"
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                    }
                }

                SizeSlider {
                    width: parent.width
                    height: 30
                    from: 0.05
                    to: 1.0
                    value: panel.prefs.buddySfxVolume
                    onMoved: (v) => panel.prefs.buddySfxVolume = Math.round(v * 100) / 100
                }
            }
        }

        SettingsCardDivider {}

        SettingsToggleRow {
            label: qsTr("Solid color ring")
            sub: qsTr("Show the solid accent outline and aura ring around buddy during interactions.")
            checked: panel.prefs.buddySolidRing
            onToggled: (c) => panel.prefs.buddySolidRing = c
        }

        SettingsCardDivider {}

        SettingsToggleRow {
            label: qsTr("Show thinking")
            sub: qsTr("Show what Buddy is doing while it works: the tool steps, the draft as it is written and its reasoning. Turn off for a quiet bubble that only shows the answer. Approvals and questions always show.")
            checked: panel.prefs.buddyShowThinking
            onToggled: (c) => panel.prefs.buddyShowThinking = c
        }

        SettingsCardDivider {}

        SettingsToggleRow {
            label: qsTr("Real chat (AI provider)")
            sub: panel.realChat
                 ? qsTr("Questions go to %1 (%2). Local timers, app launch and plans still run directly.")
                      .arg(panel.labelFor(panel.prefs.buddyProvider))
                      .arg(panel.prefs.buddyModel || qsTr("no model"))
                 : qsTr("Off: answers come from built-in local routes (timers, app launch, plans, sample cards).")
            checked: panel.realChat
            onToggled: (c) => panel.setRealChat(c)
        }
    }
}
