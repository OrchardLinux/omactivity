import QtQuick

// The phone's tab bar.
Rectangle {
    id: root
    readonly property var tabs: [
        { id: "cpu", title: "CPU", icon: "cpu", enabled: true },
        { id: "memory", title: "Memory", icon: "memory", enabled: true },
        { id: "energy", title: "Energy", icon: "energy", enabled: true },
        { id: "disk", title: "Disk", icon: "disk", enabled: true },
        { id: "network", title: "Network", icon: "network", enabled: true }
    ]
    signal unavailablePicked(string id)

    color: theme.surface
    implicitHeight: 56

    Rectangle { width: parent.width; height: Ui.line; color: theme.line }
    Row {
        anchors.fill: parent
        Repeater {
            model: root.tabs
            Rectangle {
                id: tab
                required property var modelData
                readonly property bool on: processes.tab === modelData.id
                width: root.width / root.tabs.length
                height: root.height
                color: on ? theme.tint : "transparent"
                Column {
                    anchors.centerIn: parent
                    spacing: Ui.sp(3)
                    Icon {
                        name: tab.modelData.icon
                        size: Ui.sp(20)
                        color: !tab.modelData.enabled ? theme.muted : tab.on ? theme.bright : theme.foreground
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    T {
                        text: tab.modelData.title
                        font.pixelSize: theme.fontLabel
                        font.weight: tab.on ? Font.DemiBold : Font.Normal
                        color: !tab.modelData.enabled ? theme.muted : tab.on ? theme.bright : theme.foreground
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: tab.modelData.enabled ? processes.tab = tab.modelData.id : root.unavailablePicked(tab.modelData.id)
                }
            }
        }
    }
}
