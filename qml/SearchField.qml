import QtQuick

Item {
    id: root
    property alias text: input.text
    property string placeholder: "Search"
    property bool password: false
    property color fill: theme.surface
    property string leadingIcon: "search"
    readonly property bool editing: input.activeFocus
    signal accepted()

    function focusInput() { input.forceActiveFocus() }
    function clear() { input.text = "" }

    implicitHeight: Ui.controlHeight

    Rectangle {
        anchors.fill: parent
        radius: Ui.controlRadius
        color: root.fill
        border.width: 1
        border.color: input.activeFocus ? theme.accent : theme.fieldBorder
    }
    Icon {
        id: lead
        name: root.leadingIcon
        color: theme.secondary
        size: Ui.sp(18)
        anchors.left: parent.left
        anchors.leftMargin: Ui.sp(12)
        anchors.verticalCenter: parent.verticalCenter
    }
    TextInput {
        id: input
        anchors.left: lead.right
        anchors.leftMargin: Ui.sp(10)
        anchors.right: clearButton.visible ? clearButton.left : parent.right
        anchors.rightMargin: Ui.sp(8)
        anchors.verticalCenter: parent.verticalCenter
        color: theme.bright
        selectionColor: Ui.alpha(theme.accent, 0.45)
        selectedTextColor: theme.bright
        font.family: theme.fontFamily
        font.pixelSize: theme.fontText
        clip: true
        echoMode: root.password ? TextInput.Password : TextInput.Normal
        inputMethodHints: root.password ? Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                                        : Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        onAccepted: root.accepted()
        T {
            anchors.fill: parent
            visible: input.text.length === 0 && input.preeditText.length === 0
            text: root.placeholder
            color: theme.secondary
        }
    }
    // The whole field takes the tap, which also brings up the on-screen keyboard.
    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: clearButton.visible ? clearButton.width : 0
        enabled: !input.activeFocus
        onClicked: input.forceActiveFocus()
    }
    Item {
        id: clearButton
        visible: input.text.length > 0
        width: height
        height: parent.height
        anchors.right: parent.right
        Icon { anchors.centerIn: parent; name: "x"; color: theme.secondary; size: Ui.sp(16) }
        MouseArea { anchors.fill: parent; onClicked: input.text = "" }
    }
}
