import QtQuick
import Omactivity

Item {
    id: root
    property string name
    property color color: theme.foreground
    property real size: Ui.sp(18)
    property real stroke: 2

    implicitWidth: size
    implicitHeight: size

    IconItem {
        anchors.fill: parent
        path: Ui.icons[root.name] || ""
        color: root.color
        stroke: root.stroke
    }
}
