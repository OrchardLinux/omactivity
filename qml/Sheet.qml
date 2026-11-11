import QtQuick

// A modal layer: dims what is behind it and holds one card. On a phone the
// card sits at the bottom edge, elsewhere in the middle. A tap on the dimmed
// part dismisses it.
Item {
    id: root
    property bool open: false
    property string align: "center" // center, bottom, top
    readonly property bool atBottom: align === "bottom"
    property real cardWidth: Ui.sp(420)
    property real maxCardHeight: height - 2 * Ui.margin
    property color cardBorder: theme.normalBorder
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
        // card that ends between two pixel rows loses its bottom border.
        height: Math.ceil(Math.min(holder.childrenRect.height + 2 * Ui.sp(18), root.maxCardHeight))
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.atBottom ? root.height - height : root.align === "top" ? Ui.margin * 3 : (root.height - height) / 2)
        color: theme.background
        radius: root.atBottom ? 0 : theme.radius
        border.width: Ui.line
        border.color: root.cardBorder
        MouseArea { anchors.fill: parent; enabled: root.open } // taps on the card are not dismissals
        Item {
            id: holder
            x: Ui.sp(18)
            y: Ui.sp(18)
            width: parent.width - 2 * Ui.sp(18)
            height: parent.height - 2 * Ui.sp(18)
        }
    }
}
