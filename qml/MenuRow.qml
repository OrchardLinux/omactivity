import QtQuick

// One entry of a menu: a label, an optional key hint or arrow on the right,
// an optional check mark. The highlight is an inner box, inset from the
// menu's edges. A menu that follows the finger itself (RowMenu) turns
// interactive off and sets highlighted and held.
Item {
    id: root
    property string text
    property string hint          // right-aligned, e.g. a key
    property string trailingIcon
    property bool checked: false
    property bool checkable: false
    property color tint: theme.foreground
    property bool highlighted: false
    property bool held: false
    property bool interactive: true
    signal clicked()

    readonly property bool lit: highlighted || (interactive && ma.containsMouse)
    readonly property bool down: held || (interactive && ma.pressed)

    implicitHeight: 44
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        radius: Ui.controlRadius
        color: !root.enabled ? "transparent" : root.down ? theme.pressed : root.lit ? theme.tintStrong : "transparent"
    }
    Icon {
        id: lead
        visible: root.checkable
        name: root.checked ? "check" : ""
        color: theme.accent
        size: 16
        x: 14
        anchors.verticalCenter: parent.verticalCenter
    }
    T {
        anchors.left: parent.left
        anchors.leftMargin: root.checkable ? 14 + 16 + 10 : 14
        anchors.right: trail.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: !root.enabled ? theme.secondary : root.lit && root.tint === theme.foreground ? theme.bright : root.tint
        opacity: root.enabled ? 1 : 0.6
    }
    Row {
        id: trail
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        T {
            visible: root.hint.length > 0
            text: root.hint
            color: theme.secondary
            font.pixelSize: theme.fontLabel
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
        enabled: root.interactive
        hoverEnabled: root.interactive
        onClicked: root.clicked()
    }
}
