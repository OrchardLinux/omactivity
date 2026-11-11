import QtQuick

// The column header and the process list. The name column stays in place;
// when the other columns don't fit they scroll sideways, by dragging a row
// or the header. Quit and Force Quit are in the long-press menu and the
// inspect panel, not behind a swipe.
Item {
    id: table

    property bool narrow: false
    property bool selectMode: false
    property bool rowPressed: false
    // Set by a long press: the finger that opened the menu must not go on
    // scrolling the list underneath until it lifts.
    property bool heldForMenu: false

    signal rowTapped(string key, int kind)
    signal rowMenu(string key)

    readonly property var cols: processes.columns
    readonly property real colsWidth: {
        var w = Ui.sp(8)
        for (var i = 0; i < cols.length; i++)
            w += Ui.sp(cols[i].width)
        return w
    }
    // When all columns fit, the name gives up room for them down to a
    // minimum. When they don't fit and scroll sideways anyway, the name
    // keeps a width that leaves names readable.
    readonly property real nameMin: Ui.sp(narrow ? 176 : colsWidth > width - Ui.sp(160) ? 240 : 160)
    // A phone shows the first two columns whole rather than two and a slice
    // of the third.
    readonly property real cellsWidth: narrow && cols.length > 2
        ? Math.min(Ui.sp(cols[0].width + cols[1].width), Math.max(Ui.sp(90), width - nameMin))
        : Math.min(colsWidth, Math.max(Ui.sp(90), width - nameMin))
    readonly property real nameWidth: width - cellsWidth
    readonly property real hOffset: headerFlick.contentX
    readonly property bool overflow: colsWidth > cellsWidth + 1
    readonly property real scrollY: list.contentY

    function showKey(key) {
        var i = processes.indexOfKey(key)
        if (i >= 0)
            list.positionViewAtIndex(i, ListView.Contain)
    }

    function scrollTo(y) { list.contentY = y }

    onColsChanged: headerFlick.contentX = 0

    // What a sideways drag on a row moves; the header follows it.
    Item {
        id: hSlider
        onXChanged: if (table.rowPressed) headerFlick.contentX = Math.max(0, -x)
    }

    // Rows stay where they are while a finger is on the list.
    Binding {
        target: processes
        property: "hold"
        value: list.dragging || list.flicking || table.rowPressed
    }

    Rectangle {
        id: header
        width: parent.width
        height: Ui.headerHeight
        color: theme.surface
        z: 2

        Item {
            width: table.nameWidth
            height: parent.height
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Ui.margin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Ui.sp(6)
                T {
                    text: "Process Name"
                    font.pixelSize: theme.fontSubtitle
                    color: processes.sortKey === "name" ? theme.accent : theme.foreground
                    font.weight: processes.sortKey === "name" ? Font.Bold : Font.Normal
                }
                Icon {
                    visible: processes.sortKey === "name"
                    name: processes.sortDescending ? "arrow-down" : "arrow-up"
                    color: theme.accent
                    size: Ui.sp(14)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea { anchors.fill: parent; onClicked: processes.sortBy("name") }
        }

        Flickable {
            id: headerFlick
            x: table.nameWidth
            width: table.cellsWidth
            height: parent.height
            contentWidth: table.colsWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: table.overflow
            clip: true
            Row {
                height: parent.height
                Repeater {
                    model: table.cols
                    Item {
                        id: head
                        required property var modelData
                        readonly property bool sorted: processes.sortKey === modelData.key
                        readonly property bool leftAligned: modelData.left === true
                        width: Ui.sp(modelData.width)
                        height: header.height
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: head.leftAligned ? undefined : parent.right
                            anchors.left: head.leftAligned ? parent.left : undefined
                            anchors.rightMargin: Ui.sp(12)
                            anchors.leftMargin: Ui.sp(16)
                            spacing: Ui.sp(4)
                            T {
                                text: head.modelData.title
                                font.pixelSize: theme.fontSubtitle
                                color: head.sorted ? theme.accent : theme.foreground
                                font.weight: head.sorted ? Font.Bold : Font.Normal
                            }
                            Icon {
                                visible: head.sorted
                                name: processes.sortDescending ? "arrow-down" : "arrow-up"
                                color: theme.accent
                                size: Ui.sp(14)
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: processes.sortBy(head.modelData.key) }
                    }
                }
            }
        }
        // More columns to the right. Sits in the last header's padding.
        Icon {
            visible: table.overflow && !headerFlick.atXEnd
            anchors.right: parent.right
            anchors.rightMargin: Ui.sp(1)
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            color: theme.dim
            size: Ui.sp(11)
        }
        Rectangle { width: parent.width; height: Ui.line; color: theme.separator }
        Rectangle { width: parent.width; height: Ui.line; color: theme.separator; anchors.bottom: parent.bottom }
    }

    T {
        // Shown in the gap that opens when the list is pulled down.
        anchors.horizontalCenter: parent.horizontalCenter
        y: header.height + Ui.sp(14)
        text: list.contentY < -Ui.sp(64) ? "Release to refresh" : "Pull to refresh"
        color: theme.dim
        font.pixelSize: theme.fontBody
        opacity: Math.max(0, Math.min(1, -list.contentY / Ui.sp(64)))
    }

    T {
        anchors.centerIn: list
        visible: list.count === 0
        text: processes.search.length > 0 ? "No process matches “" + processes.search + "”"
            : processes.view === "selected" ? "No rows selected"
            : processes.view === "ports" ? "None of your own processes listens on a port" : "Nothing to show in this view"
        color: theme.dim
        width: Math.min(implicitWidth, table.width - 2 * Ui.margin)
    }

    // Ports whose process cannot be told: reading another user's open
    // sockets needs root, which this app never uses for reading.
    Rectangle {
        id: portsNote
        visible: processes.view === "ports" && processes.otherPorts.length > 0
        anchors.bottom: parent.bottom
        width: parent.width
        height: visible ? portsNoteText.implicitHeight + Ui.sp(20) : 0
        color: theme.surface
        z: 2
        Rectangle { width: parent.width; height: Ui.line; color: theme.separator }
        T {
            id: portsNoteText
            x: Ui.margin
            y: Ui.sp(10)
            width: parent.width - 2 * Ui.margin
            text: "Also listening, owned by other users (which process is not readable without root):\n" + processes.otherPorts
            color: theme.dim
            font.pixelSize: theme.fontBody
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            maximumLineCount: 4
        }
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: portsNote.top
        width: parent.width
        clip: true
        model: processes
        boundsBehavior: Flickable.DragOverBounds
        interactive: !table.heldForMenu
        cacheBuffer: Ui.rowHeight * 4
        pixelAligned: true

        property bool refreshArmed: false
        onContentYChanged: if (dragging && contentY < -Ui.sp(64)) refreshArmed = true
        onDraggingChanged: {
            if (dragging) {
                refreshArmed = false
            } else if (refreshArmed) {
                refreshArmed = false
                collector.refreshNow()
            }
        }
        // Two fingers: spread for comfortable rows, pinch for compact.
        PinchHandler {
            target: null
            onActiveChanged: {
                if (active)
                    return
                if (activeScale > 1.2)
                    prefs.comfortable = true
                else if (activeScale < 0.83)
                    prefs.comfortable = false
            }
        }

        delegate: Item {
            id: row
            required property int index
            required property string key
            required property int kind // 0 process, 1 app, 2 group
            required property int depth
            required property bool expandable
            required property bool expanded
            required property int count
            required property string name
            required property string glyph
            required property bool selected
            required property bool paused
            required property bool isProtected
            required property bool ended
            required property bool notResponding
            required property var cells
            required property bool hot
            required property string blocked

            readonly property bool inApps: processes.view === "apps" || processes.view === "services"
            readonly property bool inTree: processes.view === "tree"
            readonly property bool actionable: kind !== 2 && !ended

            width: ListView.view.width
            height: Ui.rowHeight
            opacity: ended ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: collector.interval * 900 } }

            Rectangle {
                id: content
                width: row.width
                height: row.height
                color: theme.background

                Rectangle {
                    anchors.fill: parent
                    color: row.selected ? theme.selectedFill : rowMa.pressed ? theme.normalFill : "transparent"
                }
                Rectangle { visible: row.selected; width: parent.width; height: Ui.line; color: theme.normalBorder }
                Rectangle {
                    width: parent.width
                    height: Ui.line
                    anchors.bottom: parent.bottom
                    color: row.selected ? theme.normalBorder : Ui.alpha(theme.foreground, 0.06)
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    // A sideways drag on a row scrolls the columns, like
                    // dragging the header.
                    drag.target: table.overflow && !table.heldForMenu ? hSlider : null
                    drag.axis: Drag.XAxis
                    drag.minimumX: -(table.colsWidth - table.cellsWidth)
                    drag.maximumX: 0
                    drag.threshold: Ui.sp(8)
                    drag.filterChildren: true
                    // A finger that travelled is a drag, not a tap, also when
                    // there was nothing to scroll.
                    property real pressX: 0
                    property real pressY: 0
                    property bool travelled: false
                    onPositionChanged: mouse => {
                        if (Math.abs(mouse.x - pressX) > Ui.sp(10) || Math.abs(mouse.y - pressY) > Ui.sp(10))
                            travelled = true
                    }
                    onPressed: mouse => { pressX = mouse.x; pressY = mouse.y; travelled = false }
                    onPressedChanged: {
                        if (pressed)
                            hSlider.x = -headerFlick.contentX
                        else
                            table.heldForMenu = false
                        table.rowPressed = pressed
                    }
                    onCanceled: table.heldForMenu = false
                    onClicked: mouse => {
                        if (travelled)
                            return
                        if (row.kind === 2 || (row.expandable && mouse.x < lead.x + lead.chevronSlot + Ui.sp(6)))
                            processes.toggleExpanded(row.key)
                        else
                            table.rowTapped(row.key, row.kind)
                    }
                    onPressAndHold: {
                        if (!drag.active && !travelled && row.actionable) {
                            table.heldForMenu = true
                            table.rowMenu(row.key)
                        }
                    }
                }

                Row {
                    id: lead
                    readonly property real chevronSlot: (row.inApps && row.depth === 0) || row.inTree ? Ui.sp(22) : 0
                    x: Ui.margin + (row.inTree ? row.depth * Ui.sp(16) : row.inApps && row.depth > 0 ? Ui.sp(32) : 0)
                    width: Math.max(0, table.nameWidth - x - Ui.sp(6))
                    height: parent.height
                    spacing: Ui.sp(8)

                    Icon {
                        visible: table.selectMode && row.kind !== 2
                        name: row.selected ? "check-square" : "square"
                        color: row.selected ? theme.accent : theme.dim
                        size: Ui.sp(18)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item {
                        visible: lead.chevronSlot > 0
                        width: lead.chevronSlot - lead.spacing
                        height: parent.height
                        Icon {
                            visible: row.expandable
                            anchors.centerIn: parent
                            name: row.expanded ? "chevron-down" : "chevron-right"
                            color: theme.dim
                            size: Ui.sp(16)
                        }
                    }
                    Icon {
                        visible: row.inApps
                        name: row.depth > 0 ? "corner" : (row.glyph || "app")
                        color: row.depth > 0 ? theme.muted : Ui.glyphColor(row.glyph)
                        size: row.depth > 0 ? Ui.sp(16) : Ui.sp(20)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    T {
                        id: nameText
                        text: row.name
                        width: Math.min(implicitWidth, Math.max(Ui.sp(40), lead.width - x - badges.width - (badges.width > 0 ? lead.spacing : 0)))
                        color: row.selected ? theme.accent : row.kind === 0 && row.inApps ? theme.foreground : theme.bright
                        font.weight: row.kind !== 0 ? Font.Bold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Row {
                        id: badges
                        spacing: Ui.sp(6)
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            visible: row.kind === 2 || (row.kind === 1 && row.expandable && !row.expanded)
                            width: countText.implicitWidth + Ui.sp(12)
                            height: Ui.sp(20)
                            color: "transparent"
                            border.width: Ui.line
                            border.color: theme.muted
                            anchors.verticalCenter: parent.verticalCenter
                            T { id: countText; anchors.centerIn: parent; text: row.count; color: theme.dim; font.pixelSize: theme.fontSmall }
                        }
                        Rectangle {
                            visible: row.paused
                            width: pausedRow.implicitWidth + Ui.sp(12)
                            height: Ui.sp(20)
                            color: Ui.alpha(theme.yellow, 0.1)
                            border.width: Ui.line
                            border.color: theme.yellow
                            anchors.verticalCenter: parent.verticalCenter
                            Row {
                                id: pausedRow
                                anchors.centerIn: parent
                                spacing: Ui.sp(4)
                                Icon { name: "pause"; color: theme.yellow; size: Ui.sp(11); anchors.verticalCenter: parent.verticalCenter }
                                T { text: "Paused"; color: theme.yellow; font.pixelSize: theme.fontSmall; visible: !table.narrow }
                            }
                        }
                        Rectangle {
                            // Held up by the disk, or by a busy processor.
                            visible: row.blocked.length > 0
                            width: blockedText.implicitWidth + Ui.sp(12)
                            height: Ui.sp(20)
                            color: Ui.alpha(theme.orange, 0.1)
                            border.width: Ui.line
                            border.color: theme.orange
                            anchors.verticalCenter: parent.verticalCenter
                            T {
                                id: blockedText
                                anchors.centerIn: parent
                                text: row.blocked === "io" ? "I/O wait" : "CPU wait"
                                color: theme.orange
                                font.pixelSize: theme.fontSmall
                            }
                        }
                        Icon {
                            visible: row.isProtected
                            name: "shield"
                            color: theme.dim
                            size: Ui.sp(15)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        T {
                            visible: row.notResponding && !table.narrow
                            text: "Not responding ·"
                            color: theme.red
                            font.pixelSize: theme.fontBody
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            visible: row.notResponding
                            width: forceText.implicitWidth + Ui.sp(14)
                            height: Ui.sp(26)
                            color: Ui.alpha(theme.red, forceMa.pressed ? 0.3 : 0.12)
                            border.width: Ui.line
                            border.color: theme.red
                            anchors.verticalCenter: parent.verticalCenter
                            T { id: forceText; anchors.centerIn: parent; text: "Force Quit"; color: theme.red; font.pixelSize: theme.fontBody }
                            MouseArea {
                                id: forceMa
                                anchors.fill: parent
                                anchors.margins: -Ui.sp(9)
                                onClicked: actions.forceQuit([row.key])
                            }
                        }
                    }
                }

                Item {
                    x: table.nameWidth
                    width: table.cellsWidth
                    height: parent.height
                    clip: true
                    Row {
                        x: -table.hOffset
                        height: parent.height
                        Repeater {
                            model: table.cols
                            T {
                                required property var modelData
                                required property int index
                                readonly property bool leftAligned: modelData.left === true
                                width: Ui.sp(modelData.width)
                                height: row.height
                                leftPadding: leftAligned ? Ui.sp(16) : 0
                                rightPadding: leftAligned ? 0 : Ui.sp(12)
                                horizontalAlignment: leftAligned ? Text.AlignLeft : Text.AlignRight
                                text: row.cells[index] || ""
                                color: text === "—" ? theme.muted : index === 0 && row.hot ? theme.bright : theme.foreground
                                font.weight: index === 0 && row.hot ? Font.Bold : Font.Normal
                            }
                        }
                    }
                }
            }
        }
    }
}
