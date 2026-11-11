import QtQuick

// Everything about one process, with the action buttons. Shown as a side
// panel on wide windows and inside a sheet otherwise.
Rectangle {
    id: root
    property bool compact: false // phone: fewer rows above the fold
    property string page: "info"
    readonly property var info: inspector.info
    readonly property bool usable: info.valid === true && !info.ended
    readonly property string blocked: usable ? actions.disabledReason(inspector.key) : ""

    signal closeRequested()
    signal moreRequested(real right, real bottom) // the More button's corner, in the window
    signal jump(string key)

    color: theme.surface

    function scrollTo(y) { scroll.contentY = y }

    onPageChanged: {
        if (page === "files" && !inspector.filesLoaded)
            inspector.loadFiles()
    }
    Connections {
        target: inspector
        function onKeyChanged() {
            root.page = "info"
            filesFilter.clear()
            scroll.contentY = 0
        }
    }

    // On the left edge when it is a side panel; a sheet draws its own top line.
    Column {
        id: head
        x: Ui.margin
        y: Ui.margin
        width: parent.width - 2 * Ui.margin
        spacing: 12

        Item {
            width: parent.width
            height: 44
            Rectangle {
                id: iconBox
                width: 44
                height: 44
                color: theme.raised
                radius: Ui.controlRadius
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; name: root.info.glyph || "app"; color: Ui.glyphColor(root.info.glyph); size: Ui.sp(22) }
            }
            Column {
                anchors.left: iconBox.right
                anchors.leftMargin: Ui.sp(12)
                anchors.right: closeButton.left
                anchors.rightMargin: Ui.sp(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Ui.sp(2)
                T {
                    width: parent.width
                    text: root.info.title || ""
                    color: theme.bright
                    font.pixelSize: theme.fontTitle
                    font.weight: Font.DemiBold
                }
                T {
                    width: parent.width
                    text: "PID " + (root.info.pid || "") + " · " + (root.info.user || "")
                          + (root.info.appName && root.info.appName !== root.info.title ? " · " + root.info.appName : "")
                          + (root.info.isApp && root.info.count > 1 ? " · " + root.info.count + " processes" : "")
                    color: theme.secondary
                    font.pixelSize: theme.fontMeta
                }
            }
            Btn {
                id: closeButton
                icon: "x"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.closeRequested()
            }
        }

        Row {
            width: parent.width
            spacing: Ui.sp(8)
            readonly property real buttonWidth: (width - moreButton.width - 2 * spacing) / 2
            Btn {
                width: parent.buttonWidth
                text: "Quit"
                icon: "x-circle"
                kind: "primary"
                enabled: root.usable && root.blocked.length === 0
                onClicked: actions.quit([inspector.key])
            }
            Btn {
                width: parent.buttonWidth
                text: "Force Quit"
                icon: "x-circle"
                kind: "danger"
                enabled: root.usable && root.blocked.length === 0
                onClicked: actions.forceQuit([inspector.key])
            }
            Btn {
                id: moreButton
                icon: "more"
                enabled: root.usable && root.blocked.length === 0
                onClicked: {
                    var p = moreButton.mapToItem(null, moreButton.width, moreButton.height)
                    root.moreRequested(p.x, p.y)
                }
            }
        }
        T {
            visible: root.blocked.length > 0 || root.info.ended === true || root.info.isProtected === true
            width: parent.width
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: root.info.ended ? "This process has ended."
                : root.blocked.length > 0 ? root.blocked
                : "Protected: quitting it asks first."
            color: root.info.ended || root.blocked.length > 0 ? theme.yellow : theme.secondary
            font.pixelSize: theme.fontMeta
        }

        Segmented {
            width: parent.width
            fill: true
            current: root.page
            model: [
                { id: "info", title: "Info" },
                { id: "files", title: "Files" },
                { id: "env", title: "Env" },
                { id: "children", title: "Children" }
            ]
            onPicked: id => root.page = id
        }
    }

    Flickable {
        id: scroll
        anchors.top: head.bottom
        anchors.topMargin: Ui.sp(4)
        anchors.bottom: parent.bottom
        x: Ui.margin
        width: parent.width - 2 * Ui.margin
        contentHeight: pages.height + Ui.margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: pages
            width: parent.width
            height: root.page === "info" ? infoPage.height : root.page === "files" ? filesPage.height
                  : root.page === "env" ? envPage.height : childrenPage.height

            // ---- Info ----
            Column {
                id: infoPage
                visible: root.page === "info"
                width: parent.width

                Section { width: parent.width; title: "PROCESS" }
                StatRow {
                    width: parent.width
                    label: "Parent"
                    value: root.info.parent || "—"
                    link: (root.info.parentKey || "").length > 0
                    onActivated: root.jump(root.info.parentKey)
                }
                StatRow { width: parent.width; label: "User"; value: root.info.userLabel || "" }
                StatRow { width: parent.width; label: "State"; value: root.info.state || "" }
                StatRow {
                    width: parent.width
                    label: root.compact ? "Elapsed" : "Started · Elapsed"
                    value: root.compact ? (root.info.elapsed || "") : (root.info.started || "") + " · " + (root.info.elapsed || "")
                }
                StatRow { visible: !root.compact; width: parent.width; label: "Process group"; value: root.info.pgrp || "" }
                StatRow { width: parent.width; label: "App"; value: root.info.cgroup || "—" }
                StatRow { width: parent.width; label: "Unit"; value: root.info.unit || "—" }
                StatRow {
                    visible: (root.info.sleepBlock || "").length > 0
                    width: parent.width
                    label: "Prevents sleep"
                    value: root.info.sleepBlock || ""
                    valueColor: theme.yellow
                }
                StatRow { visible: !root.compact; width: parent.width; label: "Working directory"; value: root.info.cwd || "—" }
                Item { width: 1; height: Ui.sp(6) }
                Rectangle {
                    width: parent.width
                    height: cmdText.implicitHeight + Ui.sp(20)
                    color: theme.well
                    radius: Ui.controlRadius
                    T {
                        id: cmdText
                        x: Ui.sp(12)
                        y: Ui.sp(10)
                        width: parent.width - Ui.sp(24)
                        text: root.info.cmdline || ""
                        font.pixelSize: theme.fontMeta
                        wrapMode: Text.WrapAnywhere
                        elide: Text.ElideNone
                        // The whole command line, however long.
                        maximumLineCount: 200
                    }
                }

                Section { width: parent.width; title: "CPU" }
                Item {
                    width: parent.width
                    height: Ui.sp(64)
                    Column {
                        id: cpuBig
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Ui.sp(2)
                        T { text: (root.info.cpu || "0.0") + "%"; color: theme.bright; font.pixelSize: theme.fontDisplay; font.weight: Font.DemiBold }
                        T {
                            text: (root.info.cpuTime || "") + " · " + (root.info.threads || 0) + " thr · nice " + (root.info.nice || 0)
                            color: theme.secondary
                            font.pixelSize: theme.fontMeta
                        }
                    }
                    BarGraph {
                        anchors.left: cpuBig.right
                        anchors.leftMargin: Ui.sp(16)
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: Ui.sp(4)
                        anchors.bottomMargin: Ui.sp(4)
                        capacity: inspector.historyLength
                        values: inspector.cpuHistory
                    }
                }

                StatRow { width: parent.width; label: "Waiting for a CPU"; value: root.info.cpuWait || "" }
                StatRow { width: parent.width; label: "Waiting for disk I/O"; value: root.info.ioWait || "" }

                Section { width: parent.width; title: "MEMORY" }
                StatRow { width: parent.width; label: "Real"; value: root.info.memReal || "" }
                StatRow { width: parent.width; label: "Private"; value: root.info.memPrivate || "" }
                StatRow { width: parent.width; label: "Shared"; value: root.info.memShared || "" }
                StatRow { width: parent.width; label: "Swap"; value: root.info.memSwap || "" }
                StatRow { width: parent.width; label: "Virtual"; value: root.info.memVirtual || "" }

                Section { width: parent.width; title: "STATISTICS" }
                StatRow { width: parent.width; label: "Context switches"; value: root.info.ctxt || "" }
                StatRow { width: parent.width; label: "Page faults"; value: root.info.faults || "" }
                StatRow { width: parent.width; label: "Read · Written"; value: root.info.io || "" }
            }

            // ---- Files ----
            Column {
                id: filesPage
                visible: root.page === "files"
                width: parent.width
                spacing: Ui.sp(6)
                readonly property var shown: {
                    var all = inspector.files
                    var q = filesFilter.text.toLowerCase()
                    if (q.length === 0)
                        return all
                    return all.filter(f => f.text.toLowerCase().indexOf(q) >= 0 || f.detail.toLowerCase().indexOf(q) >= 0)
                }
                Item { width: 1; height: Ui.sp(6) }
                SearchField {
                    id: filesFilter
                    width: parent.width
                    visible: inspector.filesNote.length === 0
                    placeholder: "Search " + inspector.files.length + " open files and ports"
                }
                T {
                    visible: inspector.filesNote.length > 0 || (inspector.filesLoaded && filesPage.shown.length === 0)
                    text: inspector.filesNote.length > 0 ? inspector.filesNote : "Nothing open"
                    color: theme.secondary
                    topPadding: Ui.sp(12)
                }
                Repeater {
                    model: filesPage.shown
                    Item {
                        id: fileRow
                        required property var modelData
                        width: filesPage.width
                        height: fileText.implicitHeight + Ui.sp(14)
                        T {
                            id: fdText
                            text: fileRow.modelData.fd
                            color: theme.muted
                            width: Ui.sp(34)
                            font.pixelSize: theme.fontMeta
                            anchors.top: fileText.top
                        }
                        T {
                            id: fileText
                            anchors.left: fdText.right
                            anchors.right: parent.right
                            y: Ui.sp(7)
                            text: fileRow.modelData.text + (fileRow.modelData.detail ? "   " + fileRow.modelData.detail : "")
                            color: fileRow.modelData.kind === "socket" ? theme.accent : fileRow.modelData.kind === "file" ? theme.foreground : theme.secondary
                            font.pixelSize: theme.fontMeta
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 3
                        }
                    }
                }
            }

            // ---- Environment ----
            Column {
                id: envPage
                visible: root.page === "env"
                width: parent.width
                spacing: Ui.sp(6)
                Item { width: 1; height: Ui.sp(6) }
                T {
                    visible: !inspector.environmentLoaded
                    width: parent.width
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: "Environment variables can hold passwords and tokens, so they stay hidden until you ask."
                    color: theme.secondary
                    font.pixelSize: theme.fontMeta
                }
                Btn {
                    visible: !inspector.environmentLoaded
                    text: "Show environment"
                    enabled: root.usable
                    onClicked: inspector.loadEnvironment()
                }
                T {
                    visible: inspector.environmentNote.length > 0
                    text: inspector.environmentNote
                    color: theme.secondary
                    topPadding: Ui.sp(6)
                }
                Repeater {
                    model: inspector.environment
                    Item {
                        id: envRow
                        required property string modelData
                        readonly property int eq: modelData.indexOf("=")
                        width: envPage.width
                        height: envName.implicitHeight + envValue.implicitHeight + Ui.sp(14)
                        T { id: envName; y: Ui.sp(6); text: envRow.modelData.substring(0, envRow.eq); color: theme.accent; font.pixelSize: theme.fontMeta; width: parent.width }
                        T {
                            id: envValue
                            anchors.top: envName.bottom
                            width: parent.width
                            text: envRow.modelData.substring(envRow.eq + 1)
                            font.pixelSize: theme.fontMeta
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 4
                        }
                    }
                }
            }

            // ---- Children ----
            Column {
                id: childrenPage
                visible: root.page === "children"
                width: parent.width
                Item { width: 1; height: Ui.sp(6) }
                T {
                    visible: inspector.children.length === 0
                    text: "No child processes"
                    color: theme.secondary
                    topPadding: Ui.sp(12)
                }
                Repeater {
                    model: inspector.children
                    Item {
                        id: childRow
                        required property var modelData
                        width: childrenPage.width
                        height: Math.max(48, Ui.sp(48))
                        Rectangle { anchors.fill: parent; color: childMa.pressed ? theme.pressed : "transparent" }
                        T {
                            anchors.left: parent.left
                            anchors.right: childNumbers.left
                            anchors.rightMargin: Ui.sp(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: childRow.modelData.name
                            color: theme.accent
                        }
                        T {
                            id: childNumbers
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: childRow.modelData.cpu + "%   " + childRow.modelData.memory + "   " + childRow.modelData.pid
                            color: theme.secondary
                            font.pixelSize: theme.fontMeta
                        }
                        MouseArea { id: childMa; anchors.fill: parent; onClicked: root.jump(childRow.modelData.key) }
                    }
                }
            }
        }
    }
}
