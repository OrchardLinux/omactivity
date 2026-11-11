import QtQuick

// "PROCESS ————————" section heading in the inspect panel.
Item {
    property string title
    implicitHeight: Ui.sp(30)
    T {
        id: label
        text: parent.title
        color: theme.dim
        font.pixelSize: theme.fontSmall
        font.letterSpacing: 1.5
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.sp(4)
    }
    Rectangle {
        anchors.left: label.right
        anchors.leftMargin: Ui.sp(10)
        anchors.right: parent.right
        anchors.verticalCenter: label.verticalCenter
        height: Ui.line
        color: theme.separator
    }
}
