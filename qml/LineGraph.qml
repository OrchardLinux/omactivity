import QtQuick
import Omactivity

// One line per core, for the optional per-core CPU graph.
Rectangle {
    id: root
    property var series: [] // list of lists, 0..100, oldest first
    property int capacity: 60

    color: theme.well
    radius: Ui.controlRadius

    LinesItem {
        anchors.fill: parent
        anchors.margins: 4
        series: root.series
        capacity: root.capacity
        colors: [theme.accent, theme.green, theme.orange, theme.magenta, theme.cyan, theme.yellow, theme.red, theme.foreground]
    }
}
