pragma ComponentBehavior: Bound
import QtQuick

SettingsPage {
    id: page
    required property var panel
    title: qsTr("Apps")
    subtitle: qsTr("Pinned in the notch, and every installed app you can add.")
    maxContentWidth: 800
Item {
id: appsView
width: parent.width
implicitHeight: panel.appsListH + 72
height: implicitHeight

readonly property real colGap: 14
readonly property real leftW: Math.floor((width - colGap) * 0.47)
readonly property real rightW: width - leftW - colGap

// ── Left Column: Pinned in Notch ──────────────────────────────
Column {
    x: 0
    y: 16
    width: appsView.leftW
    spacing: 10

    Item {
        width: parent.width
        height: 28

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("PINNED (%1)").arg(panel.dockModel.length)
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            SettingsPillButton {
                icon: "reset"
                label: qsTr("Defaults")
                onClicked: panel.resetRequested()
            }
            SettingsPillButton {
                icon: "folder"
                label: qsTr("Browse…")
                onClicked: panel.addRequested()
            }
        }
    }

    Rectangle {
        width: parent.width
        height: panel.appsListH
        radius: 14
        color: Theme.raised
        border.width: 0
        border.color: Theme.borderSoft
        clip: true

        Text {
            visible: panel.dockModel.length === 0
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("No apps pinned yet.\nClick + Add on the right to pin apps.")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
        }

        ListView {
            id: pinnedList
            visible: panel.dockModel.length > 0
            x: 6
            y: 6
            width: parent.width - 12
            height: parent.height - 12
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 320
            model: panel.dockModel
            spacing: 2

            displaced: Transition {
                NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
            }

            delegate: Item {
                id: appRow
                required property int index
                required property var modelData
                width: pinnedList.width
                height: 40

                readonly property bool isRunning: panel.runningList.indexOf(modelData.exe) >= 0

                HoverHandler { id: rowHover }

                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: rowHover.hovered ? Theme.hover : "transparent"
                }

                Item {
                    id: rowIcon
                    x: 10
                    width: 22
                    height: 22
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: rowImg
                        anchors.fill: parent
                        source: appRow.modelData.iconId
                                ? "image://appicon/" + (panel.prefs.monoIcons ? Theme.iconMono : "color") + "/" + appRow.modelData.iconId
                                : ""
                        sourceSize: Qt.size(96, 96)
                        mipmap: true
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: rowImg.status !== Image.Ready
                        radius: 6
                        color: Theme.tile
                        border.width: 0
                        border.color: Theme.border
                        Glyph {
                            anchors.centerIn: parent
                            name: Theme.appGlyph(appRow.modelData.name)
                            size: Math.round(parent.height * 0.58)
                            stroke: 1.6
                            color: Theme.inkSoft
                        }
                    }
                }

                Row {
                    anchors.left: rowIcon.right
                    anchors.leftMargin: 10
                    anchors.right: rowActions.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Text {
                        width: Math.min(implicitWidth, parent.width - (appRow.isRunning ? 14 : 0))
                        text: appRow.modelData.name
                        color: Theme.ink
                        elide: Text.ElideRight
                        font.family: Theme.font
                        font.pixelSize: Theme.fontMd
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        visible: appRow.isRunning
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.success
                    }
                }

                Row {
                    id: rowActions
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    opacity: rowHover.hovered ? 1 : 0.65
                    Behavior on opacity { NumberAnimation { duration: 110 } }

                    SettingsIconButton {
                        icon: "up"
                        glyphSize: 13
                        visible: appRow.index > 0
                        onClicked: panel.moveRequested(appRow.index, appRow.index - 1)
                    }
                    SettingsIconButton {
                        icon: "down"
                        glyphSize: 13
                        visible: appRow.index < panel.dockModel.length - 1
                        onClicked: panel.moveRequested(appRow.index, appRow.index + 1)
                    }
                    SettingsIconButton {
                        icon: "close"
                        glyphSize: 13
                        danger: true
                        onClicked: panel.unpinRequested(appRow.index)
                    }
                }
            }
        }
    }
}

// ── Right Column: Installed Apps List to Add ──────────────────
Column {
    x: appsView.leftW + appsView.colGap
    y: 16
    width: appsView.rightW
    spacing: 10

    Item {
        width: parent.width
        height: 28

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("ADD APPS")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.Bold
            font.letterSpacing: 0.8
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 available").arg(panel.availableList.length)
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
        }
    }

    Rectangle {
        width: parent.width
        height: panel.appsListH
        radius: 14
        color: Theme.raised
        border.width: 0
        border.color: Theme.borderSoft
        clip: true

        // Search bar pinned at the top of the right card
        Rectangle {
            id: filterBar
            x: 10
            y: 10
            width: parent.width - 20
            height: 34
            radius: 9
            color: Theme.field
            border.width: 1
            border.color: filterInput.activeFocus ? Theme.accent : Theme.fieldBorder
            Behavior on border.color { ColorAnimation { duration: 130 } }

            Glyph {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 13
                stroke: 2
                color: filterInput.activeFocus ? Theme.accent : Theme.textDim
            }

            TextInput {
                id: filterInput
                anchors.left: parent.left
                anchors.leftMargin: 30
                anchors.right: clearFilterBtn.visible ? clearFilterBtn.left : parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink
                selectionColor: Theme.accent
                selectedTextColor: Theme.inverse
                font.family: Theme.font
                font.pixelSize: Theme.fontMd
                font.weight: Font.Medium
                clip: true
                selectByMouse: true
                onTextChanged: panel.appFilter = text

                Text {
                    anchors.fill: parent
                    visible: filterInput.text.length === 0
                    verticalAlignment: TextInput.AlignVCenter
                    text: qsTr("Search installed apps…")
                    color: Theme.textFaint
                    font: filterInput.font
                }
            }

            SettingsIconButton {
                id: clearFilterBtn
                visible: filterInput.text.length > 0
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                icon: "close"
                glyphSize: 11
                onClicked: {
                    filterInput.text = ""
                    filterInput.forceActiveFocus()
                }
            }
        }

        Text {
            visible: panel.availableList.length === 0
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 16
            text: filterInput.text.length > 0
                  ? qsTr("No matching unpinned apps found")
                  : qsTr("All detected apps are already pinned")
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fontBase
        }

        ListView {
            id: addList
            visible: panel.availableList.length > 0
            x: 6
            y: 50
            width: parent.width - 12
            height: parent.height - 56
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 320
            model: panel.availableList
            spacing: 2

            displaced: Transition {
                NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic }
            }

            delegate: Item {
                id: candRow
                required property int index
                required property var modelData
                width: addList.width
                height: 40

                HoverHandler { id: candHover }

                Rectangle {
                    anchors.fill: parent
                    radius: 9
                    color: candHover.hovered ? Theme.hover : "transparent"
                }

                Item {
                    id: candIcon
                    x: 10
                    width: 22
                    height: 22
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: candImg
                        anchors.fill: parent
                        source: candRow.modelData.iconId
                                ? "image://appicon/color/" + candRow.modelData.iconId
                                : ""
                        sourceSize: Qt.size(96, 96)
                        mipmap: true
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: candImg.status !== Image.Ready
                        radius: 6
                        color: Theme.tile
                        border.width: 0
                        border.color: Theme.border
                        Glyph {
                            anchors.centerIn: parent
                            name: Theme.appGlyph(candRow.modelData.name)
                            size: Math.round(parent.height * 0.58)
                            stroke: 1.6
                            color: Theme.inkSoft
                        }
                    }
                }

                Text {
                    anchors.left: candIcon.right
                    anchors.leftMargin: 10
                    anchors.right: addPill.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: candRow.modelData.name
                    color: Theme.ink
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    id: addPill
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: addPillRow.implicitWidth + 16
                    height: 25
                    radius: 12.5
                    color: candHover.hovered ? Theme.accent : Theme.tile
                    border.width: candHover.hovered ? 0 : 1
                    border.color: Theme.border
                    scale: candTap.pressed ? 0.94 : 1
                    Behavior on scale { NumberAnimation { duration: 100 } }
                    Behavior on color { ColorAnimation { duration: 110 } }

                    Row {
                        id: addPillRow
                        anchors.centerIn: parent
                        spacing: 4
                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "plus"
                            size: 11
                            stroke: 2.2
                            color: candHover.hovered ? Theme.inverse : Theme.ink
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Add")
                            color: candHover.hovered ? Theme.inverse : Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.Bold
                        }
                    }
                }

                TapHandler {
                    id: candTap
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: panel.pinAppRequested(candRow.modelData.name, candRow.modelData.path)
                }
            }
        }
    }
}
}
}
