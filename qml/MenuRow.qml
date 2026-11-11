import QtQuick

// One row of a menu sheet.
Item {
    id: root
    property string text
    property string detail
    property string icon
    property bool checked: false
    property bool checkable: false
    property color tint: theme.foreground
    signal clicked()

    implicitHeight: Math.max(48, Ui.sp(detail.length > 0 ? 56 : 48))
    opacity: enabled ? 1 : 0.4
    Rectangle { anchors.fill: parent; color: ma.pressed ? theme.pressedFill : root.checked ? theme.selectedFill : "transparent" }
    Icon {
        id: lead
        visible: root.icon.length > 0 || root.checkable
        name: root.checkable ? (root.checked ? "check" : "") : root.icon
        color: root.checkable ? theme.accent : root.tint
        size: Ui.sp(18)
        x: Ui.sp(10)
        anchors.verticalCenter: parent.verticalCenter
    }
    Column {
        anchors.left: lead.visible ? lead.right : parent.left
        anchors.leftMargin: Ui.sp(12)
        anchors.right: parent.right
        anchors.rightMargin: Ui.sp(10)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Ui.sp(2)
        T { width: parent.width; text: root.text; color: root.checked ? theme.accent : root.tint }
        T { width: parent.width; visible: root.detail.length > 0; text: root.detail; color: theme.dim; font.pixelSize: theme.fontBody }
    }
    MouseArea { id: ma; anchors.fill: parent; onClicked: root.clicked() }
}
