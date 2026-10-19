pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    default property alias cardContent: innerCol.data
    width: parent ? parent.width : 340
    implicitHeight: innerCol.implicitHeight + 2 * Theme.sp4
    radius: Theme.radiusLg
    color: Theme.raised

    Column {
        id: innerCol
        x: Theme.sp4
        y: Theme.sp4
        width: parent.width - 2 * Theme.sp4
        spacing: Theme.sp3
    }
}
