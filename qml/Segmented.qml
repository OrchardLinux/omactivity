import QtQuick

// A strip of joined segments: the tabs under the toolbar and the inspect
// panel's Info / Files / Env / Children.
Item {
    id: root
    property var model: [] // [{ id, title, enabled }]
    property string current
    property bool fill: false // segments share the width equally
    signal picked(string id)
    signal unavailablePicked(string id) // a tap on a segment that is not usable yet

    implicitHeight: Ui.controlHeight
    implicitWidth: row.implicitWidth

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: theme.radius
        border.width: Ui.line
        border.color: theme.normalBorder
    }
    Row {
        id: row
        height: parent.height
        Repeater {
            model: root.model
            Item {
                id: seg
                required property var modelData
                required property int index
                readonly property bool on: modelData.id === root.current
                readonly property bool usable: modelData.enabled !== false
                height: row.height
                width: root.fill ? root.width / root.model.length : label.implicitWidth + 2 * Ui.sp(18)
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Ui.line
                    color: seg.on ? theme.selectedFill : segMa.pressed && seg.usable ? theme.pressedFill : "transparent"
                }
                Rectangle {
                    visible: seg.index > 0
                    width: Ui.line
                    height: parent.height
                    color: theme.normalBorder
                }
                T {
                    id: label
                    anchors.centerIn: parent
                    text: seg.modelData.title
                    color: !seg.usable ? theme.muted : seg.on ? theme.bright : theme.foreground
                    font.weight: seg.on ? Font.Bold : Font.Normal
                }
                MouseArea {
                    id: segMa
                    anchors.fill: parent
                    anchors.topMargin: -Ui.sp(4)
                    anchors.bottomMargin: -Ui.sp(4)
                    onClicked: seg.usable ? root.picked(seg.modelData.id) : root.unavailablePicked(seg.modelData.id)
                }
            }
        }
    }
}
