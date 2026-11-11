import QtQuick

// Buttons are fills, not outlines. normal: a quiet fill. primary: solid
// accent, one per surface. danger: a red tint. Without a text the button is
// an icon alone on nothing, unless flat is turned off.
Item {
    id: root
    property string text
    property string icon
    property string trailingIcon
    property string kind: "normal" // normal, primary, danger
    property bool selected: false
    property bool busy: false
    property bool flat: text.length === 0
    readonly property bool pressed: ma.pressed
    readonly property color ink: kind === "primary" ? theme.accentForeground : kind === "danger" ? theme.red
                                 : selected ? theme.bright : theme.foreground
    signal clicked()

    implicitHeight: Ui.controlHeight
    implicitWidth: text.length > 0 ? content.implicitWidth + 2 * 16 : Ui.controlHeight
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true

    Rectangle {
        anchors.fill: parent
        radius: Ui.controlRadius
        color: root.kind === "primary" ? theme.accent
             : root.kind === "danger" ? theme.redTint
             : root.selected ? theme.tint
             : root.flat ? "transparent" : theme.quiet
        // Pressed lies over the fill, so it shows on every kind.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: theme.pressed
            visible: ma.pressed
        }
        // Keyboard focus: nothing else gives a button the focus.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: root.kind === "primary" ? theme.bright : theme.accent
            visible: root.activeFocus
        }
    }
    Row {
        id: content
        anchors.centerIn: parent
        spacing: 8
        Icon {
            visible: root.icon.length > 0
            name: root.icon
            color: root.ink
            size: 18
            anchors.verticalCenter: parent.verticalCenter
        }
        T {
            visible: root.text.length > 0
            text: root.text
            color: root.ink
            font.weight: root.kind === "primary" ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: root.trailingIcon.length > 0
            name: root.trailingIcon
            color: theme.secondary
            size: 14
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        onClicked: root.clicked()
    }
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
