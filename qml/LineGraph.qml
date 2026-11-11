import QtQuick
import Omactivity

// One line per core, for the optional per-core CPU graph.
Rectangle {
    id: root
    property var series: [] // list of lists, 0..100, oldest first
    property int capacity: 60

    color: "transparent"
    border.width: Ui.line
    border.color: theme.separator

    LinesItem {
        anchors.fill: parent
        anchors.margins: Ui.sp(3)
        series: root.series
        capacity: root.capacity
        colors: [theme.accent, theme.green, theme.orange, theme.magenta, theme.cyan, theme.yellow, theme.red, theme.foreground]
    }
}
