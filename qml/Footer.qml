import QtQuick

// System totals with a live graph, for the current tab. On a phone it
// shrinks to three headline numbers and the graph.
Rectangle {
    id: root
    property bool narrow: false
    readonly property string tab: processes.tab
    readonly property bool memory: tab === "memory"

    color: theme.surface
    implicitHeight: narrow ? Ui.sp(96) : Ui.sp(memory ? 138 : 126)

    Rectangle { width: parent.width; height: Ui.line; color: theme.line }

    function pressureColor(v) {
        if (!system.pressureAvailable)
            return v >= 90 ? theme.red : v >= 75 ? theme.yellow : theme.green
        return v >= 40 ? theme.red : v >= 10 ? theme.yellow : theme.green
    }

    // ---- CPU ----
    Item {
        visible: root.tab === "cpu"
        anchors.fill: parent
        anchors.margins: Ui.margin
        anchors.topMargin: Ui.sp(root.narrow ? 12 : 14)
        anchors.bottomMargin: Ui.sp(root.narrow ? 12 : 14)

        Column {
            id: cpuNumbers
            width: Ui.sp(root.narrow ? 120 : 172)
            anchors.verticalCenter: parent.verticalCenter
            StatRow { size: theme.fontMeta; width: parent.width; label: "System"; value: system.systemPercent.toFixed(1) + "%"; valueColor: theme.red }
            StatRow { size: theme.fontMeta; width: parent.width; label: "User"; value: system.userPercent.toFixed(1) + "%"; valueColor: theme.accent }
            StatRow { size: theme.fontMeta; width: parent.width; label: "Idle"; value: system.idlePercent.toFixed(1) + "%" }
        }
        Item {
            id: cpuGraphBox
            anchors.left: cpuNumbers.right
            anchors.leftMargin: Ui.sp(root.narrow ? 14 : 24)
            anchors.right: root.narrow ? parent.right : cpuStats.left
            anchors.rightMargin: root.narrow ? 0 : Ui.sp(24)
            height: parent.height
            Item {
                id: cpuTitle
                visible: !root.narrow
                width: parent.width
                height: visible ? 20 : 0
                T { text: "CPU LOAD"; color: theme.secondary; font.pixelSize: theme.fontLabel; font.letterSpacing: 1.3 }
                T {
                    visible: cpuGraphBox.width > Ui.sp(150)
                    // The scale moves with the load, so say what the top is.
                    text: "top " + (prefs.perCore ? 100 : Math.round(cpuBars.scaleTop)) + "% · 2 min"
                    color: theme.secondary
                    font.pixelSize: theme.fontCaption
                    anchors.right: parent.right
                }
            }
            BarGraph {
                id: cpuBars
                visible: !prefs.perCore
                anchors.top: cpuTitle.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                capacity: system.historyLength
                values: system.userHistory
                lower: system.systemHistory
                barColor: theme.accent
                lowerColor: theme.red
            }
            LineGraph {
                visible: prefs.perCore
                anchors.top: cpuTitle.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                capacity: system.historyLength
                series: prefs.perCore ? system.coreHistories : []
            }
        }
        Column {
            id: cpuStats
            visible: !root.narrow
            width: Ui.sp(250)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(20); label: "Load avg"; value: system.loadAverage }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(20); label: "Processes"; value: system.processes }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(20); label: "Threads"; value: system.threads }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(20); label: "Frequency"; value: system.frequency || "—" }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(20); label: "Uptime"; value: system.uptime }
        }
    }

    // ---- Memory ----
    Item {
        visible: root.memory
        anchors.fill: parent
        anchors.margins: Ui.margin
        anchors.topMargin: Ui.sp(root.narrow ? 12 : 14)
        anchors.bottomMargin: Ui.sp(root.narrow ? 12 : 14)

        Column {
            id: memNumbers
            visible: root.narrow
            width: Ui.sp(150)
            anchors.verticalCenter: parent.verticalCenter
            StatRow { size: theme.fontMeta; width: parent.width; label: "Used"; value: system.memUsed }
            StatRow { size: theme.fontMeta; width: parent.width; label: "Swap"; value: system.swapUsed }
            StatRow {
                size: theme.fontMeta
                width: parent.width
                label: "Pressure"
                value: system.pressureAvailable ? system.pressureSome.toFixed(1) : "—"
                valueColor: root.pressureColor(system.pressureSome)
            }
        }
        Item {
            anchors.left: root.narrow ? memNumbers.right : parent.left
            anchors.leftMargin: root.narrow ? Ui.sp(14) : 0
            anchors.right: root.narrow ? parent.right : memStats.left
            anchors.rightMargin: root.narrow ? 0 : Ui.sp(24)
            height: parent.height
            Item {
                id: memTitle
                visible: !root.narrow
                width: parent.width
                height: visible ? Ui.sp(18) : 0
                T { id: memTitleText; text: "MEMORY PRESSURE"; color: theme.secondary; font.pixelSize: theme.fontLabel; font.letterSpacing: 1.3 }
                T {
                    visible: memTitle.width > memTitleText.implicitWidth + implicitWidth + 12
                    text: system.memUsedPercent.toFixed(0) + "% used" + (system.pressureAvailable ? " · " + system.pressureNow : "")
                    color: theme.secondary
                    font.pixelSize: theme.fontCaption
                    anchors.right: parent.right
                }
            }
            // As in Activity Monitor: the height is how full memory is, the
            // colour is how hard the system has to work for it (the kernel's
            // pressure figure). Green and half full is fine; red is not.
            BarGraph {
                anchors.top: memTitle.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                capacity: system.historyLength
                values: system.memUsedHistory
                fixedTop: 100
                levels: system.pressureHistory
                colorFor: root.pressureColor
            }
        }
        Column {
            id: memStats
            visible: !root.narrow
            width: Ui.sp(310)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(19); label: "Physical Memory"; value: system.memTotal }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(19); label: "Memory Used"; value: system.memUsed }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(19); label: "Cached Files"; value: system.memCached }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(19); label: "Swap Used"; value: system.swapUsed }
            StatRow { size: theme.fontMeta; width: parent.width; implicitHeight: Ui.sp(19); label: "Compressed"; value: system.compressed || "—" }
            StatRow {
                size: theme.fontMeta
                width: parent.width
                implicitHeight: Ui.sp(19)
                label: "Pressure 1 min · 5 min"
                value: system.pressureAvailable ? system.pressureAverages : "—"
            }
        }
    }

    // ---- Energy, Disk, Network: three numbers, a graph, a column of totals ----
    component Totals: Item {
        id: totals
        property var numbers: []   // [{ label, value, color }] shown on a phone and at the left
        property var stats: []     // [{ label, value }] at the right, not on a phone
        property string title
        property string note
        property var values: []
        property var lower: []
        property real minTop: 1
        property color barColor: theme.accent
        property color lowerColor: theme.red

        anchors.fill: parent
        anchors.margins: Ui.margin
        anchors.topMargin: Ui.sp(root.narrow ? 12 : 14)
        anchors.bottomMargin: Ui.sp(root.narrow ? 12 : 14)

        Column {
            id: totalsNumbers
            width: Ui.sp(root.narrow ? 150 : 190)
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: totals.numbers
                StatRow {
                    size: theme.fontMeta
                    required property var modelData
                    width: totalsNumbers.width
                    label: modelData.label
                    value: modelData.value
                    valueColor: modelData.color || theme.bright
                }
            }
        }
        Item {
            id: totalsGraph
            anchors.left: totalsNumbers.right
            anchors.leftMargin: Ui.sp(root.narrow ? 14 : 24)
            anchors.right: root.narrow ? parent.right : totalsStats.left
            anchors.rightMargin: root.narrow ? 0 : Ui.sp(24)
            height: parent.height
            Item {
                id: totalsTitle
                visible: !root.narrow
                width: parent.width
                height: visible ? Ui.sp(18) : 0
                T { text: totals.title; color: theme.secondary; font.pixelSize: theme.fontLabel; font.letterSpacing: 1.3 }
                T {
                    visible: totalsGraph.width > Ui.sp(190)
                    text: totals.note
                    color: theme.secondary
                    font.pixelSize: theme.fontCaption
                    anchors.right: parent.right
                }
            }
            BarGraph {
                anchors.top: totalsTitle.bottom
                anchors.bottom: parent.bottom
                width: parent.width
                capacity: system.historyLength
                values: totals.values
                lower: totals.lower
                minTop: totals.minTop
                barColor: totals.barColor
                lowerColor: totals.lowerColor
            }
        }
        Column {
            id: totalsStats
            visible: !root.narrow
            width: Ui.sp(290)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: totals.stats
                StatRow {
                    size: theme.fontMeta
                    required property var modelData
                    width: totalsStats.width
                    implicitHeight: Ui.sp(20)
                    label: modelData.label
                    value: modelData.value
                }
            }
        }
    }

    Totals {
        visible: root.tab === "energy"
        title: "POWER DRAW"
        note: system.powerDraw
        values: system.powerHistory
        minTop: 0.1
        barColor: theme.yellow
        numbers: [
            { label: "Battery", value: system.batteryLevel },
            { label: "State", value: system.batteryState },
            { label: "Power", value: system.powerDraw, color: theme.yellow }
        ]
        stats: [
            { label: "Battery level", value: system.batteryLevel },
            { label: "State", value: system.batteryState },
            { label: system.batteryTimeLabel, value: system.batteryTime },
            { label: "Power draw", value: system.powerDraw },
            { label: "Load avg", value: system.loadAverage }
        ]
    }
    Totals {
        visible: root.tab === "disk"
        title: "DATA READ AND WRITTEN"
        note: "2 min"
        values: system.diskReadHistory
        lower: system.diskWriteHistory
        minTop: 4096
        numbers: [
            { label: "Read", value: system.diskReadRate, color: theme.accent },
            { label: "Written", value: system.diskWriteRate, color: theme.red },
            { label: "Free", value: system.diskFree.split(" of ")[0] }
        ]
        stats: [
            { label: "Reads in / s", value: system.diskReads },
            { label: "Writes out / s", value: system.diskWrites },
            { label: "Data read", value: system.diskReadRate },
            { label: "Data written", value: system.diskWriteRate },
            { label: "Free on /", value: system.diskFree }
        ]
    }
    Totals {
        visible: root.tab === "network"
        title: "DATA RECEIVED AND SENT"
        note: system.netInterface
        values: system.netRxHistory
        lower: system.netTxHistory
        minTop: 1024
        numbers: [
            { label: "Received", value: system.netRxRate, color: theme.accent },
            { label: "Sent", value: system.netTxRate, color: theme.red },
            { label: "Interface", value: system.netInterface }
        ]
        stats: [
            { label: "Data received", value: system.netRxRate },
            { label: "Data sent", value: system.netTxRate },
            { label: "Received since start", value: system.netRxTotal },
            { label: "Sent since start", value: system.netTxTotal },
            { label: "Interface", value: system.netInterface }
        ]
    }
}
