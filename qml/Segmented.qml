import QtQuick

// A row of quiet text tabs: the tabs under the toolbar, the inspect panel's
// Info / Files / Env / Children, and the choices in the view sheet. The
// current one is tinted; there is no outline and nothing between them.
Item {
    id: root
    property var model: [] // [{ id, title, enabled }]
    property string current
    property bool fill: false // tabs share the width equally
    signal picked(string id)
    signal unavailablePicked(string id) // a tap on a tab that is not usable yet

    implicitHeight: Ui.controlHeight
    implicitWidth: row.implicitWidth

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
                width: root.fill ? Math.floor(root.width / root.model.length) : Math.ceil(label.implicitWidth) + 2 * 16
                Rectangle {
                    anchors.fill: parent
                    radius: Ui.controlRadius
                    color: seg.on ? theme.tint : segMa.pressed && seg.usable ? theme.pressed : "transparent"
                }
                T {
                    id: label
                    anchors.centerIn: parent
                    text: seg.modelData.title
                    color: !seg.usable ? theme.muted : seg.on ? theme.bright : theme.foreground
                    font.weight: seg.on ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: segMa
                    anchors.fill: parent
                    onClicked: seg.usable ? root.picked(seg.modelData.id) : root.unavailablePicked(seg.modelData.id)
                }
            }
        }
    }
}
