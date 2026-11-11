import QtQuick
import QtQuick.Window

Window {
    id: win

    // Opens maximized: on a tablet or phone there is nothing to gain from a
    // small window. (A fixed size is only asked for when taking pictures.)
    visibility: devWidth > 0 ? Window.Windowed : Window.Maximized
    width: devWidth > 0 ? devWidth : 1024
    height: devHeight > 0 ? devHeight : 768
    minimumWidth: 320
    minimumHeight: 420
    color: theme.background
    title: "Omactivity"

    // The layout follows the window width, not the device, so a split or
    // resized window gets the right one.
    readonly property string mode: width >= 1000 ? "wide" : width >= 600 ? "medium" : "narrow"
    readonly property bool narrow: mode === "narrow"
    readonly property bool wide: mode === "wide"

    property bool inspectOpen: false
    property bool selectMode: false
    property string overlay: ""  // "", "view", "actions" (the long-press menu), "priority"
    property string menuKey: ""  // the row the action menus are about
    property var menuInfo: ({})
    property int niceChoice: 0
    readonly property var prompt: actions.prompt
    readonly property bool asking: prompt.type !== undefined
    readonly property bool modal: overlay !== "" || asking
    // True while any text field has the focus. The list's keys (arrows,
    // Delete = Quit) must not fire while someone is typing.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    readonly property var shortViews: ({
        "apps": "Apps", "all": "All", "tree": "Tree", "mine": "Mine", "system": "System",
        "others": "Others", "active": "Active", "windowed": "Windowed", "ports": "Ports",
        "services": "Services", "selected": "Selected"
    })

    onDevScrollChanged: table.scrollTo(devScroll)
    onNarrowChanged: processes.narrow = narrow
    Component.onCompleted: {
        processes.narrow = narrow
        if (devShow.length > 0)
            devTimer.start()
    }

    function openInspector(key) {
        var info = processes.rowInfo(key)
        if (!info.valid)
            return
        processes.select(key)
        inspector.open(key)
        inspectOpen = true
        prefs.hintSeen = true
    }
    function closeInspector() {
        inspectOpen = false
        inspector.close()
    }
    // The menu for a row. With rowY it lifts that row (window coordinates);
    // without, it hangs from the corner (right, bottom) of a button.
    function openActions(key, rowY, value, title, right, bottom) {
        var info = processes.rowInfo(key)
        if (!info.valid)
            return
        menuKey = key
        menuInfo = info
        rowMenu.rowY = rowY === undefined ? -1 : rowY
        rowMenu.value = value || ""
        rowMenu.valueTitle = title || ""
        rowMenu.anchorRight = right === undefined ? win.width - Ui.margin : right
        rowMenu.anchorY = bottom === undefined ? toolbar.height : bottom
        overlay = "actions"
    }
    function jumpTo(key) {
        processes.reveal(key)
        table.showKey(key)
        inspector.open(key)
    }
    function toast(text, error) {
        toastText.text = text
        toastBox.error = error
        toastBox.shown = true
        toastTimer.restart()
    }

    Connections {
        target: actions
        function onMessage(text, error) { win.toast(text, error) }
        function onSent(keys, ends) {
            // After a Force Quit there is nothing left to inspect: the panel
            // closes with the process. The list catches up at once.
            if (ends && win.inspectOpen && keys.indexOf(inspector.key) >= 0)
                win.closeInspector()
            collector.refreshNow()
        }
        function onPromptChanged() {
            if (win.prompt.type === "password") {
                passwordField.clear()
                passwordField.focusInput()
            } else if (win.prompt.type === "confirm") {
                cancelButton.forceActiveFocus()
            }
        }
    }
    Connections {
        target: processes
        function onCurrentKeyChanged() {
            // The side panel follows the selection while it is open.
            if (win.inspectOpen && win.wide && !win.selectMode && processes.currentKey.length > 0
                    && processes.currentKey !== inspector.key && processes.rowInfo(processes.currentKey).valid)
                inspector.open(processes.currentKey)
        }
    }

    // ------------------------------------------------------------ keyboard
    Shortcut { sequence: "Ctrl+F"; onActivated: searchField.focusInput() }
    Shortcut { sequence: "Up"; enabled: !win.modal && !win.typing; onActivated: { processes.moveCurrent(-1); table.showKey(processes.currentKey) } }
    Shortcut { sequence: "Down"; enabled: !win.modal && !win.typing; onActivated: { processes.moveCurrent(1); table.showKey(processes.currentKey) } }
    Shortcut { sequence: "Left"; enabled: !win.modal && !win.typing; onActivated: processes.toggleExpanded(processes.currentKey) }
    Shortcut { sequence: "Right"; enabled: !win.modal && !win.typing; onActivated: processes.toggleExpanded(processes.currentKey) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: !win.modal && !win.typing && processes.currentKey.length > 0; onActivated: win.openInspector(processes.currentKey) }
    Shortcut { sequence: "Delete"; enabled: !win.modal && !win.typing && processes.selectionCount > 0; onActivated: actions.quit(processes.selectedKeys) }
    Shortcut { sequence: "Shift+Delete"; enabled: !win.modal && !win.typing && processes.selectionCount > 0; onActivated: actions.forceQuit(processes.selectedKeys) }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (win.asking) actions.cancel()
            else if (win.overlay === "actions") rowMenu.back()
            else if (win.overlay !== "") win.overlay = ""
            else if (searchField.editing || searchField.text.length > 0) { searchField.clear(); keyCatcher.forceActiveFocus() }
            else if (win.inspectOpen) win.closeInspector()
            else if (win.selectMode) win.selectMode = false
        }
    }
    Shortcut { sequence: "Up"; enabled: win.overlay === "actions" && !win.asking; onActivated: rowMenu.move(-1) }
    Shortcut { sequence: "Down"; enabled: win.overlay === "actions" && !win.asking; onActivated: rowMenu.move(1) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: win.overlay === "actions" && !win.asking; onActivated: rowMenu.activate() }
    Item { id: keyCatcher; focus: true }

    // ------------------------------------------------------------ main column
    Item {
        id: main
        height: parent.height
        width: win.wide && win.inspectOpen ? parent.width - sidePanel.width : parent.width

        Item {
            id: toolbar
            width: parent.width
            height: 76

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Ui.margin
                anchors.right: tools.left
                anchors.rightMargin: Ui.sp(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Ui.sp(3)
                T {
                    width: parent.width
                    text: "Omactivity"
                    color: theme.bright
                    font.pixelSize: theme.fontTitle
                    font.weight: Font.DemiBold
                }
                T {
                    width: parent.width
                    text: win.selectMode ? processes.selectionCount + " selected"
                        : processes.search.length > 0 ? processes.matchCount + " of " + processes.processCount + " processes match"
                        : win.narrow ? win.shortViews[processes.view] + " · " + processes.processCount + " · " + collector.interval + " s"
                        : processes.viewTitle + " · " + processes.processCount + " processes · every " + collector.interval + " s"
                    color: theme.secondary
                    font.pixelSize: theme.fontMeta
                }
            }
            Row {
                id: tools
                anchors.right: parent.right
                anchors.rightMargin: Ui.margin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Ui.sp(8)
                Btn {
                    icon: "list-tree"
                    selected: win.overlay === "view"
                    text: win.narrow ? "" : win.shortViews[processes.view]
                    trailingIcon: win.narrow ? "" : "chevron-down"
                    onClicked: win.overlay = "view"
                }
                Btn {
                    text: win.selectMode ? "Done" : "Select"
                    selected: win.selectMode
                    onClicked: {
                        win.selectMode = !win.selectMode
                        if (win.selectMode)
                            win.closeInspector()
                    }
                }
                Btn {
                    visible: !win.narrow
                    icon: "info"
                    selected: win.inspectOpen
                    enabled: processes.selectionCount === 1 || win.inspectOpen
                    onClicked: win.inspectOpen ? win.closeInspector() : win.openInspector(processes.currentKey)
                }
                Btn {
                    icon: "x-circle"
                    text: win.narrow ? "" : "Quit"
                    flat: false // the one solid Quit is in the inspect header
                    enabled: processes.selectionCount > 0
                    onClicked: actions.quit(processes.selectedKeys)
                }
            }
        }

        Item {
            id: filterRow
            anchors.top: toolbar.bottom
            width: parent.width
            height: Ui.controlHeight + 12
            Segmented {
                id: tabs
                visible: !win.narrow
                x: Ui.margin
                current: processes.tab
                model: [
                    { id: "cpu", title: "CPU" },
                    { id: "memory", title: "Memory" },
                    { id: "energy", title: "Energy" },
                    { id: "disk", title: "Disk" },
                    { id: "network", title: "Network" }
                ]
                onPicked: id => processes.tab = id
            }
            SearchField {
                id: searchField
                anchors.left: win.narrow ? parent.left : tabs.right
                anchors.leftMargin: win.narrow ? Ui.margin : Ui.sp(12)
                anchors.right: parent.right
                anchors.rightMargin: Ui.margin
                placeholder: width < Ui.sp(220) ? "Search" : "Search processes"
                onTextChanged: processes.search = text
                onAccepted: keyCatcher.forceActiveFocus()
            }
        }

        ProcessTable {
            id: table
            objectName: "table"
            anchors.top: filterRow.bottom
            anchors.bottom: hint.top
            width: parent.width
            narrow: win.narrow
            selectMode: win.selectMode
            frozen: win.overlay === "actions"
            onRowTapped: (key, kind) => {
                keyCatcher.forceActiveFocus()
                if (win.selectMode)
                    processes.toggleSelected(key)
                else
                    win.openInspector(key)
            }
            onRowMenu: (key, rowY, value, title) => win.openActions(key, table.y + rowY, value, title)
        }

        // Shown until a process has been opened once.
        Rectangle {
            id: hint
            visible: !prefs.hintSeen && !win.selectMode
            anchors.bottom: footer.top
            width: parent.width
            height: visible ? hintText.implicitHeight + Ui.sp(18) : 0
            color: theme.raised
            T {
                id: hintText
                x: Ui.margin
                y: Ui.sp(9)
                width: parent.width - 2 * Ui.margin
                text: "Tap a group to open it, tap a process for details, hold one to quit it and more. Drag sideways for more columns. Columns and views are under the button at the top right."
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                font.pixelSize: theme.fontMeta
            }
            MouseArea { anchors.fill: parent; onClicked: prefs.hintSeen = true }
        }

        Footer {
            id: footer
            anchors.bottom: bottomTabs.visible ? bottomTabs.top : parent.bottom
            width: parent.width
            narrow: win.narrow
        }
        BottomTabs {
            id: bottomTabs
            visible: win.narrow
            anchors.bottom: parent.bottom
            width: parent.width
        }
    }

    // ------------------------------------------------------------ inspect
    // Wide: a side panel, the list stays visible.
    Item {
        id: sidePanel
        visible: win.wide && win.inspectOpen
        anchors.right: parent.right
        width: Math.max(Ui.sp(380), Math.round(win.width * 0.37))
        height: parent.height
        Loader {
            anchors.fill: parent
            active: sidePanel.visible
            sourceComponent: InspectPanel {
                id: sideInspect
                Connections {
                    target: win
                    function onDevPageChanged() { sideInspect.page = win.devPage }
                    function onDevScrollChanged() { sideInspect.scrollTo(win.devScroll) }
                }
                onCloseRequested: win.closeInspector()
                onMoreRequested: (right, bottom) => win.openActions(inspector.key, undefined, "", "", right, bottom)
                onJump: key => win.jumpTo(key)
            }
        }
        Rectangle { width: 1; height: parent.height; color: theme.line }
    }

    // Medium and narrow: a sheet that slides up over the dimmed list.
    Item {
        id: inspectSheet
        readonly property bool shown: !win.wide && win.inspectOpen
        anchors.fill: parent
        visible: slide.y < height
        Rectangle {
            anchors.fill: parent
            color: theme.scrim
            opacity: inspectSheet.shown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            MouseArea { anchors.fill: parent; enabled: inspectSheet.shown; onClicked: win.closeInspector() }
        }
        Item {
            id: slide
            readonly property real restY: win.narrow ? toolbar.height + filterRow.height - Ui.sp(6) : Math.round(win.height * 0.34)
            width: parent.width
            height: parent.height - restY
            y: inspectSheet.shown ? restY : parent.height
            Behavior on y {
                enabled: !handleMa.drag.active
                NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
            }
            Loader {
                anchors.fill: parent
                active: inspectSheet.visible || inspectSheet.shown
                sourceComponent: InspectPanel {
                    id: sheetInspect
                    Connections {
                        target: win
                        function onDevPageChanged() { sheetInspect.page = win.devPage }
                        function onDevScrollChanged() { sheetInspect.scrollTo(win.devScroll) }
                    }
                    compact: win.narrow
                    onCloseRequested: win.closeInspector()
                    onMoreRequested: (right, bottom) => win.openActions(inspector.key, undefined, "", "", right, bottom)
                    onJump: key => win.jumpTo(key)
                }
            }
            Rectangle { width: parent.width; height: 1; color: theme.line }
            // The grab handle: drag down, or tap, to close.
            Item {
                width: Ui.sp(120)
                height: Ui.sp(16)
                anchors.horizontalCenter: parent.horizontalCenter
                Rectangle { width: 36; height: 4; radius: 2; color: theme.handle; anchors.centerIn: parent }
                MouseArea {
                    id: handleMa
                    anchors.fill: parent
                    anchors.margins: -Ui.sp(8)
                    drag.target: slide
                    drag.axis: Drag.YAxis
                    drag.minimumY: slide.restY
                    drag.maximumY: inspectSheet.height
                    onReleased: {
                        if (slide.y > slide.restY + Ui.sp(80))
                            win.closeInspector()
                        slide.y = Qt.binding(() => inspectSheet.shown ? slide.restY : inspectSheet.height)
                    }
                    onClicked: win.closeInspector()
                }
            }
        }
    }

    // ------------------------------------------------------------ view menu
    Sheet {
        open: win.overlay === "view"
        align: win.narrow ? "bottom" : "center"
        onDismissed: win.overlay = ""
        Flickable {
            width: parent.width
            height: Math.min(viewColumn.height, win.height - 2 * Ui.margin - Ui.sp(36))
            contentHeight: viewColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: viewColumn
                width: parent.width
                Section { width: parent.width; title: "VIEW"; implicitHeight: 22 }
                Repeater {
                    model: processes.views
                    MenuRow {
                        required property var modelData
                        x: -14
                        width: viewColumn.width + 28
                        text: modelData.title
                        checkable: true
                        checked: processes.view === modelData.id
                        enabled: modelData.id !== "selected" || processes.selectionCount > 0 || checked
                        onClicked: { processes.view = modelData.id; win.overlay = "" }
                    }
                }
                Section { width: parent.width; title: "COLUMNS · " + processes.tab.toUpperCase() }
                Repeater {
                    model: processes.allColumns
                    MenuRow {
                        required property var modelData
                        x: -14
                        width: viewColumn.width + 28
                        text: modelData.title
                        checkable: true
                        checked: modelData.visible
                        enabled: !modelData.fixed
                        onClicked: processes.setColumnVisible(modelData.key, !modelData.visible)
                    }
                }
                Section { width: parent.width; title: "REFRESH" }
                Item { width: 1; height: Ui.sp(6) }
                Segmented {
                    width: parent.width
                    fill: true
                    current: String(collector.interval)
                    model: [{ id: "1", title: "1 s" }, { id: "2", title: "2 s" }, { id: "5", title: "5 s" }]
                    onPicked: id => collector.interval = Number(id)
                }
                Section { width: parent.width; title: "ROWS" }
                Item { width: 1; height: Ui.sp(6) }
                Segmented {
                    width: parent.width
                    fill: true
                    current: prefs.comfortable ? "comfortable" : "compact"
                    model: [{ id: "compact", title: "Compact" }, { id: "comfortable", title: "Comfortable" }]
                    onPicked: id => prefs.comfortable = id === "comfortable"
                }
                Section { width: parent.width; title: "CPU GRAPH" }
                Item { width: 1; height: Ui.sp(6) }
                Segmented {
                    width: parent.width
                    fill: true
                    current: prefs.perCore ? "cores" : "total"
                    model: [{ id: "total", title: "All cores" }, { id: "cores", title: "One line per core" }]
                    onPicked: id => prefs.perCore = id === "cores"
                }
            }
        }
    }

    // ------------------------------------------------------------ long-press menu
    RowMenu {
        id: rowMenu
        open: win.overlay === "actions"
        narrow: win.narrow
        info: win.menuInfo
        blocked: open ? actions.disabledReason(win.menuKey) : ""
        canInspect: !(win.inspectOpen && inspector.key === win.menuKey)
        rowX: main.x
        rowWidth: main.width
        rowHeight: Ui.rowHeight
        signalList: actions.signalList
        onDismissed: win.overlay = ""
        onPicked: (action, number) => {
            // The menu closes first, then the action runs or asks.
            var key = win.menuKey
            win.overlay = ""
            if (action === "inspect") win.openInspector(key)
            else if (action === "quit") actions.quit([key])
            else if (action === "forcequit") actions.forceQuit([key])
            else if (action === "tree") actions.quitTree(key)
            else if (action === "signal") actions.sendSignal([key], number)
            else if (action === "priority") { win.niceChoice = win.menuInfo.nice || 0; win.overlay = "priority" }
            else if (action === "pause") {
                var list = actions.signalList
                var wanted = win.menuInfo.paused ? "SIGCONT" : "SIGSTOP"
                for (var i = 0; i < list.length; i++)
                    if (list[i].name === wanted)
                        actions.sendSignal([key], list[i].number)
            }
        }
    }

    Sheet {
        open: win.overlay === "priority"
        align: win.narrow ? "bottom" : "center"
        onDismissed: win.overlay = ""
        Column {
            width: parent.width
            spacing: Ui.sp(10)
            T {
                width: parent.width
                text: "Priority of " + (win.menuInfo.name || "")
                color: theme.bright
                font.pixelSize: theme.fontTitle
                font.weight: Font.DemiBold
            }
            T {
                text: "Nice " + win.niceChoice + (win.niceChoice === (win.menuInfo.nice || 0) ? "  (now)" : "")
                color: theme.foreground
            }
            // Left is lower priority (nice 19), right is higher (nice -20).
            Item {
                id: slider
                width: parent.width
                height: Math.max(48, Ui.sp(48))
                readonly property real knob: Ui.sp(28)
                readonly property real span: width - knob
                function niceAt(px) { return Math.round(19 - Math.max(0, Math.min(1, (px - knob / 2) / span)) * 39) }
                Rectangle {
                    x: slider.knob / 2
                    width: slider.span
                    height: 4
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.pressed
                }
                Rectangle { // the Normal mark
                    x: slider.knob / 2 + slider.span * 19 / 39 - width / 2
                    width: Ui.line + 1
                    height: Ui.sp(16)
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.secondary
                }
                Rectangle {
                    x: slider.span * (19 - win.niceChoice) / 39
                    width: slider.knob
                    height: slider.knob
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.accent
                    radius: Ui.controlRadius
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onPressed: mouse => win.niceChoice = slider.niceAt(mouse.x)
                    onPositionChanged: mouse => win.niceChoice = slider.niceAt(mouse.x)
                }
            }
            Item {
                width: parent.width
                height: Ui.sp(18)
                T { text: "Lower"; color: theme.secondary; font.pixelSize: theme.fontMeta }
                T { text: "Normal"; color: theme.secondary; font.pixelSize: theme.fontMeta; x: slider.knob / 2 + slider.span * 19 / 39 - width / 2 }
                T { text: "Higher"; color: theme.secondary; font.pixelSize: theme.fontMeta; anchors.right: parent.right }
            }
            T {
                width: parent.width
                visible: win.overlay === "priority" && (win.niceChoice < (win.menuInfo.nice || 0) || actions.needsSudo(win.menuKey))
                text: "Raising a priority, or changing another user's process, is done through sudo."
                color: theme.secondary
                font.pixelSize: theme.fontMeta
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Row {
                anchors.right: parent.right
                spacing: Ui.sp(10)
                Btn { text: "Cancel"; width: Math.max(implicitWidth, 112); onClicked: win.overlay = "" }
                Btn {
                    text: "Change"
                    kind: "primary"
                    width: Math.max(implicitWidth, 112)
                    enabled: win.niceChoice !== (win.menuInfo.nice || 0)
                    onClicked: { win.overlay = ""; actions.renice(win.menuKey, win.niceChoice) }
                }
            }
        }
    }

    // ------------------------------------------------------------ confirmation
    // Folio's ConfirmDialog: a red edge for anything destructive, a quiet
    // Cancel that has the keyboard focus (Enter cancels), the action solid.
    Sheet {
        open: win.prompt.type === "confirm"
        cardBorder: win.prompt.destructive === false ? theme.accent : theme.red
        cardColor: theme.background
        cardWidth: 440
        onDismissed: actions.cancel()
        Column {
            width: parent.width
            spacing: Ui.sp(12)
            Row {
                width: parent.width
                spacing: Ui.sp(10)
                Icon { name: "alert"; color: theme.yellow; size: 18; y: 1 }
                T {
                    width: parent.width - Ui.sp(30)
                    text: win.prompt.title || ""
                    color: theme.bright
                    font.pixelSize: theme.fontDialog
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
            T {
                width: parent.width
                text: win.prompt.message || ""
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                lineHeight: 1.4
            }
            T {
                width: parent.width
                visible: (win.prompt.note || "").length > 0
                text: win.prompt.note || ""
                color: theme.secondary
                font.pixelSize: theme.fontLabel
                elide: Text.ElideMiddle
            }
            Item { width: 1; height: Ui.sp(2) }
            Row {
                anchors.right: parent.right
                spacing: Ui.sp(10)
                Btn {
                    id: cancelButton
                    text: "Cancel"
                    width: Math.max(implicitWidth, 112)
                    onClicked: actions.cancel()
                }
                Btn {
                    // Solid, not red: the edge already says it is destructive.
                    text: win.prompt.confirmLabel || "OK"
                    kind: "primary"
                    width: Math.max(implicitWidth, 112)
                    onClicked: actions.confirm()
                }
            }
        }
    }

    // ------------------------------------------------------------ sudo password
    // Near the top, so the on-screen keyboard does not cover it.
    Sheet {
        open: win.prompt.type === "password"
        align: win.height < Ui.sp(700) ? "top" : "center"
        cardBorder: (win.prompt.error || "").length > 0 ? theme.red : theme.accent
        cardColor: theme.background
        cardWidth: 440
        onDismissed: actions.cancel()
        Column {
            width: parent.width
            spacing: Ui.sp(12)
            Row {
                spacing: Ui.sp(10)
                Icon { name: "lock"; color: theme.accent; size: Ui.sp(20); anchors.verticalCenter: parent.verticalCenter }
                T { text: win.prompt.title || ""; color: theme.bright; font.pixelSize: theme.fontDialog; font.weight: Font.DemiBold }
            }
            T {
                width: parent.width
                text: win.prompt.message || ""
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                lineHeight: 1.4
            }
            SearchField {
                id: passwordField
                width: parent.width
                password: true
                leadingIcon: "lock"
                placeholder: "Password"
                enabled: !win.prompt.busy
                onAccepted: actions.submitPassword(text)
            }
            T {
                width: parent.width
                visible: (win.prompt.error || "").length > 0
                text: win.prompt.error || ""
                color: theme.red
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Row {
                anchors.right: parent.right
                spacing: Ui.sp(10)
                Btn { text: "Cancel"; width: Math.max(implicitWidth, 112); onClicked: actions.cancel() }
                Btn {
                    text: win.prompt.busy ? "Checking…" : (win.prompt.confirmLabel || "OK")
                    kind: "primary"
                    width: Math.max(implicitWidth, 112)
                    enabled: passwordField.text.length > 0 && !win.prompt.busy
                    onClicked: actions.submitPassword(passwordField.text)
                }
            }
        }
    }

    // ------------------------------------------------------------ messages
    Rectangle {
        id: toastBox
        property bool shown: false
        property bool error: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.sp(72)
        width: Math.min(toastText.implicitWidth + Ui.sp(32), parent.width - 2 * Ui.margin)
        height: toastText.implicitHeight + Ui.sp(22)
        color: theme.background
        radius: Ui.menuRadius
        border.width: 1
        border.color: error ? theme.red : theme.menuBorder
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        T {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toastBox.parent.width - 2 * Ui.margin - Ui.sp(32))
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: theme.bright
        }
        MouseArea { anchors.fill: parent; onClicked: toastBox.shown = false }
        Timer { id: toastTimer; interval: 5000; onTriggered: toastBox.shown = false }
    }

    // ------------------------------------------------------------ development
    // --show "tab:memory,expand:Firefox,inspect:Isolated Web Co" puts the
    // window in a state for --grab; it has no use outside of that.
    Timer {
        id: devTimer
        interval: 900
        onTriggered: {
            var steps = devShow.split(",")
            for (var i = 0; i < steps.length; i++) {
                var colon = steps[i].indexOf(":")
                var cmd = colon < 0 ? steps[i] : steps[i].substring(0, colon)
                var arg = colon < 0 ? "" : steps[i].substring(colon + 1)
                var key = arg.length > 0 ? processes.findByName(arg) : ""
                if (cmd === "tab") processes.tab = arg
                else if (cmd === "view") processes.view = arg
                else if (cmd === "search") searchField.text = arg
                else if (cmd === "expand") processes.toggleExpanded(key)
                else if (cmd === "inspect") win.openInspector(key)
                else if (cmd === "page") win.devPage = arg
                else if (cmd === "select") { win.selectMode = true; processes.toggleSelected(key) }
                else if (cmd === "pick") processes.select(key)
                else if (cmd === "menu") win.overlay = arg
                else if (cmd === "actions") devHoldLater(key, "main")
                else if (cmd === "priority") { win.openActions(key); win.niceChoice = -5; win.overlay = "priority" }
                else if (cmd === "signals") devHoldLater(key, "signals")
                else if (cmd === "quit") actions.quit([key])
                else if (cmd === "forcequit") actions.forceQuit([key])
                else if (cmd === "confirm") actions.confirm()
                else if (cmd === "scroll") win.devScroll = Number(arg)
            }
        }
    }
    // After the rows of a group opened in the same breath exist.
    function devHoldLater(key, page) {
        devHoldTimer.key = key
        devHoldTimer.page = page
        devHoldTimer.start()
    }
    Timer {
        id: devHoldTimer
        property string key
        property string page
        interval: 1800
        onTriggered: { table.devHold(key); rowMenu.page = page }
    }
    property string devPage: ""
    property real devScroll: 0
}
