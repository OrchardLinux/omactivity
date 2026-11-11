import QtQuick

// A history graph of plain rectangles: newest sample on the right, one bar
// per sample, an optional second series stacked at the bottom.
Rectangle {
    id: root
    property var values: []   // 0..max, oldest first
    property var lower: []    // drawn over the bottom of each bar
    property int capacity: 60
    // The top of the graph follows the tallest bar in view, so that bar
    // reaches three quarters of the height: a graph that idles at 4 % should
    // not be a thin line along the bottom. minTop keeps noise around zero
    // from being blown up to full height.
    property real minTop: 1
    property real fixedTop: 0  // above 0: a fixed scale instead (a share of a known whole)
    property var levels: []    // per bar, what colorFor is asked about instead of the bar's value
    readonly property real scaleTop: {
        var peak = 0
        for (var i = 0; i < values.length; i++)
            peak = Math.max(peak, values[i] + (i < lower.length ? lower[i] : 0))
        return fixedTop > 0 ? fixedTop : Math.max(minTop, peak / 0.75)
    }
    property color barColor: theme.accent
    property color lowerColor: theme.red
    property var colorFor: null // function(value) -> color, overrides barColor
    property real floor: 0 // least bar height in px: a sample of zero still shows as a sample

    // A box without a frame, a shade darker than what it sits on.
    color: theme.well
    radius: Ui.controlRadius
    clip: true

    function heightFor(v) {
        return Math.max(0, Math.min(1, v / root.scaleTop))
    }

    Row {
        id: bars
        anchors.fill: parent
        anchors.margins: 4
        readonly property real slot: width / root.capacity
        Repeater {
            model: root.capacity
            Item {
                required property int index
                readonly property int at: index - (root.capacity - root.values.length)
                readonly property real value: at >= 0 ? root.values[at] : 0
                readonly property real low: at >= 0 && at < root.lower.length ? root.lower[at] : 0
                width: bars.slot
                height: bars.height
                Rectangle {
                    visible: parent.at >= 0
                    anchors.bottom: parent.bottom
                    x: bars.slot > 3 ? 0.5 : 0
                    width: Math.max(1, bars.slot - (bars.slot > 3 ? 1 : 0))
                    height: Math.max(parent.value > 0 ? 1 : 0, root.floor, bars.height * root.heightFor(parent.value + parent.low))
                    color: root.colorFor ? root.colorFor(parent.at >= 0 && parent.at < root.levels.length ? root.levels[parent.at] : parent.value) : root.barColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.min(parent.height, bars.height * root.heightFor(parent.parent.low))
                        color: root.lowerColor
                        visible: parent.parent.low > 0
                    }
                }
            }
        }
    }
}
