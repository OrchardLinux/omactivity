import QtQuick

// A modal layer: dims what is behind it and holds one card. On a phone the
// card sits at the bottom edge, with a grab handle; elsewhere in the middle.
// A tap on the dimmed part dismisses it. Only dialogs give the card an edge.
Item {
    id: root
    property bool open: false
    property string align: "center" // center, bottom, top
    readonly property bool atBottom: align === "bottom"
    property real cardWidth: 420
    property real maxCardHeight: height - 2 * Ui.margin
    property color cardBorder: "transparent"
    property color cardColor: atBottom ? theme.surface : theme.background
    readonly property real pad: 18
    readonly property real topPad: atBottom ? 26 : pad
    default property alias content: holder.data
    readonly property alias card: card
    signal dismissed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    Rectangle {
        anchors.fill: parent
        color: theme.scrim
        // Only while open: a sheet that is fading out must not swallow the
        // next touch.
        MouseArea { anchors.fill: parent; enabled: root.open; onClicked: root.dismissed() }
    }
    Rectangle {
        id: card
        width: root.atBottom ? root.width : Math.min(root.cardWidth, root.width - 2 * Ui.margin)
        // Whole pixels: text heights are fractional with some fonts, and a
        // card that ends between two pixel rows loses its bottom edge.
        height: Math.ceil(Math.min(holder.childrenRect.height + root.topPad + root.pad, root.maxCardHeight))
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.atBottom ? root.height - height : root.align === "top" ? Ui.margin * 3 : (root.height - height) / 2)
        color: root.cardColor
        radius: root.atBottom ? 0 : Ui.cardRadius
        border.width: root.cardBorder.a > 0 ? 1 : 0
        border.color: root.cardBorder
        MouseArea { anchors.fill: parent; enabled: root.open } // taps on the card are not dismissals
        Rectangle { visible: root.atBottom; width: parent.width; height: 1; color: theme.line }
        Rectangle {
            visible: root.atBottom
            width: 36
            height: 4
            radius: 2
            y: 9
            anchors.horizontalCenter: parent.horizontalCenter
            color: theme.handle
        }
        Item {
            id: holder
            x: root.pad
            y: root.topPad
            width: parent.width - 2 * root.pad
            height: parent.height - root.topPad - root.pad
        }
    }
}
