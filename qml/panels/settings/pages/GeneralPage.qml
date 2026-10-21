pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("General")
    subtitle: qsTr("Size, music pill and system behaviour.")

    SettingsSectionHeader {
        title: qsTr("Size & scale")
        rightText: Math.round(panel.prefs.sizeScale * 100) + "%"
    }

    SettingsGroupCard {
        Item {
            width: parent.width
            height: 32

            Segmented {
                id: sizePresets
                width: Math.min(200, parent.width * 0.5)
                height: 32
                anchors.verticalCenter: parent.verticalCenter
                options: [
                    { label: "S", value: 0.8 },
                    { label: "M", value: 1.0 },
                    { label: "L", value: 1.2 },
                    { label: "XL", value: 1.4 }
                ]
                value: panel.prefs.sizeScale
                onPicked: (v) => panel.prefs.sizeScale = v
            }

            SizeSlider {
                anchors.left: sizePresets.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                from: 0.7
                to: 1.6
                value: panel.prefs.sizeScale
                onMoved: (v) => panel.prefs.sizeScale = Math.round(v * 100) / 100
            }
        }

        Text {
            width: parent.width
            text: qsTr("Drag the handle below the music pill, or Ctrl + scroll on it, to resize live.")
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
        }
    }

    SettingsSectionHeader { title: qsTr("Behavior & system") }

    SettingsGroupCard {
        SettingsToggleRow {
            label: qsTr("Auto-hide music pill")
            sub: qsTr("Stays out on a clear desktop and tucks into the top edge while an app window covers its space. Hover the top dash to bring it back.")
            checked: panel.prefs.autoHide
            onToggled: (c) => panel.prefs.autoHide = c
        }

        Item {
            visible: panel.prefs.autoHide
            width: parent.width
            height: 32
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hide delay")
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fontBase
                font.weight: Font.Medium
            }
            Segmented {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 268
                height: 28
                options: [
                    { label: "0.3s", value: 300 },
                    { label: "0.8s", value: 800 },
                    { label: "1.2s", value: 1200 },
                    { label: "1.5s", value: 1500 },
                    { label: "3s", value: 3000 }
                ]
                value: panel.prefs.hideDelay
                onPicked: (v) => panel.prefs.hideDelay = v
            }
        }

        SettingsCardDivider {}
        SettingsToggleRow {
            label: qsTr("Quiet in fullscreen")
            sub: qsTr("Ignore top-edge hover while a game or fullscreen video is active.")
            checked: panel.prefs.suppressInFullscreen
            onToggled: (c) => panel.prefs.suppressInFullscreen = c
        }
        SettingsCardDivider {}
        Item {
            width: parent.width
            implicitHeight: buddyToggle.implicitHeight

            SettingsToggleRow {
                id: buddyToggle
                width: parent.width - (resetPosBtn.visible ? resetPosBtn.width + 12 : 0)
                label: qsTr("Desktop buddy")
                sub: qsTr("Interactive orb companion. Drag it anywhere or click it for tasks, timers and AI chat.")
                checked: panel.prefs.buddyEnabled
                onToggled: (c) => panel.prefs.buddyEnabled = c
            }
            SettingsPillButton {
                id: resetPosBtn
                visible: panel.buddyMovedCustom
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                icon: "reset"
                label: qsTr("Reset position")
                onClicked: panel.resetBuddyPosRequested()
            }
        }
        SettingsCardDivider {}
        SettingsToggleRow {
            label: qsTr("Command palette hotkey")
            sub: !panel.prefs.hotkeyPalette
                 ? qsTr("Off. Ctrl + Alt + Space summons search from anywhere.")
                 : (!Hotkey.registered
                    ? qsTr("Shortcut unavailable on this PC.")
                    : (Hotkey.preferred
                       ? qsTr("Press Ctrl + Alt + Space anywhere to search.")
                       : qsTr("Active on %1.").arg(Hotkey.shortcut)))
            checked: panel.prefs.hotkeyPalette
            onToggled: (c) => panel.prefs.hotkeyPalette = c
        }
        SettingsCardDivider { visible: panel.startupSupported }
        SettingsToggleRow {
            visible: panel.startupSupported
            label: qsTr("Launch at startup")
            sub: qsTr("Start Noch automatically when you sign in to Windows.")
            checked: panel.launchAtStartup
            onToggled: (c) => panel.startupToggled(c)
        }
    }
}
