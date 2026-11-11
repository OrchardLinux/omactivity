import QtQuick

// "Label ........ value", used in the footers and the inspect panel.
Item {
    id: root
    property string label
    property string value
    property color valueColor: theme.bright
    property bool link: false
    property bool wrapValue: false
    signal activated()

    implicitHeight: Math.max(Ui.sp(26), valueText.implicitHeight + Ui.sp(8))
    T {
        id: labelText
        text: root.label
        color: theme.dim
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
    }
    T {
        id: valueText
        text: root.value
        color: root.link ? theme.accent : root.valueColor
        anchors.right: linkIcon.visible ? linkIcon.left : parent.right
        anchors.rightMargin: linkIcon.visible ? Ui.sp(4) : 0
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.width - labelText.implicitWidth - Ui.sp(16) - (linkIcon.visible ? linkIcon.width + Ui.sp(4) : 0))
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideMiddle
    }
    Icon {
        id: linkIcon
        visible: root.link
        name: "arrow-up-right"
        color: theme.accent
        size: Ui.sp(14)
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.link
        onClicked: root.activated()
    }
}
