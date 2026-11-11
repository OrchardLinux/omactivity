import QtQuick

Item {
    id: root
    property string text
    property string icon
    property string trailingIcon
    property string kind: "normal" // normal, primary, danger
    property bool selected: false
    property bool busy: false
    readonly property color tint: kind === "primary" ? theme.accent : kind === "danger" ? theme.red : theme.foreground
    readonly property bool pressed: ma.pressed
    signal clicked()

    implicitHeight: Ui.controlHeight
    implicitWidth: text.length > 0 ? content.implicitWidth + 2 * Ui.sp(14) : Ui.controlHeight
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true

    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: ma.pressed ? Ui.alpha(root.tint, theme.pressedFillAlpha)
             : root.selected ? Ui.alpha(root.tint, theme.selectedFillAlpha)
             : Ui.alpha(root.tint, root.kind === "normal" ? theme.normalFillAlpha : theme.normalFillAlpha * 2)
        border.width: root.activeFocus ? Ui.line + 1 : Ui.line
        border.color: root.kind === "normal" && !root.activeFocus ? theme.normalBorder : root.kind === "normal" ? theme.accent : root.tint
    }
    Row {
        id: content
        anchors.centerIn: parent
        spacing: Ui.sp(8)
        Icon {
            visible: root.icon.length > 0
            name: root.icon
            color: root.kind === "normal" ? theme.foreground : root.tint
            size: Ui.sp(18)
            anchors.verticalCenter: parent.verticalCenter
        }
        T {
            visible: root.text.length > 0
            text: root.text
            color: root.kind === "normal" ? theme.foreground : root.tint
            font.weight: root.kind === "normal" ? Font.Normal : Font.Bold
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: root.trailingIcon.length > 0
            name: root.trailingIcon
            color: theme.dim
            size: Ui.sp(14)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        // A 36 px control still gets a 44 px target.
        anchors.margins: -Math.max(0, (44 - root.height) / 2)
        onClicked: root.clicked()
    }
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
