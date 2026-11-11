import QtQuick

// The long-press menu. The pressed row is lifted above a dark scrim, where
// it was, and a compact menu opens next to it: under it, or above when
// there is no room. On a phone the four common actions are a row of icons;
// elsewhere every action is an entry. Send Signal opens its list as a
// second menu, beside the first when the window is wide enough and in its
// place otherwise. Nothing here animates.
Item {
    id: root

    property bool open: false
    property bool narrow: false
    property var info: ({})          // processes.rowInfo() of the row
    property string blocked          // why actions are not allowed, or ""
    property bool canInspect: true
    // The lifted row, in this item's coordinates. rowY < 0: no row to lift
    // (the menu was asked for by a button); the menu then hangs from
    // (anchorRight, anchorY).
    property real rowX: 0
    property real rowY: -1
    property real rowWidth: width
    property real rowHeight: 48
    property real anchorRight: width
    property real anchorY: 0
    property string value            // the sort column's value of the row
    property string valueTitle
    property string page: "main"     // main, signals
    property var signalList: []
    property int cursor: -1          // the entry the keyboard is on

    signal dismissed()
    signal picked(string action, int number) // quit, forcequit, pause, inspect, priority, tree, signal (+ number)

    readonly property bool lifted: rowY >= 0
    readonly property real menuWidth: Math.min(300, width - 16)
    readonly property bool sideBySide: !narrow && mainBox.x + 2 * menuWidth + 4 <= width - 8
    readonly property bool paused: info.paused === true

    anchors.fill: parent
    visible: open
    onOpenChanged: { page = "main"; cursor = -1 }
    onPageChanged: cursor = -1

    readonly property var mainEntries: {
        var off = blocked.length > 0
        var list = []
        if (!narrow) {
            if (canInspect)
                list.push({ id: "inspect", text: "Inspect" }, { sep: true })
            list.push({ id: "quit", text: "Quit", hint: "Delete", off: off },
                      { id: "forcequit", text: "Force Quit", hint: "Shift+Delete", red: true, off: off },
                      { sep: true },
                      { id: "pause", text: paused ? "Resume" : "Pause", off: off })
        }
        list.push({ id: "signals", text: "Send Signal", arrow: true, off: off },
                  { id: "priority", text: "Change Priority…", off: off },
                  { id: "tree", text: "Quit Process Tree", off: off })
        return list
    }
    readonly property var signalEntries: {
        var list = []
        for (var i = 0; i < signalList.length; i++) {
            var s = signalList[i]
            // The first clause of the meaning fits beside the name.
            list.push({ id: "signal", number: s.number, text: s.name, hint: s.meaning.split(/[:;,]/)[0], red: s.name === "SIGKILL" })
        }
        return list
    }
    readonly property var currentEntries: page === "signals" ? signalEntries : mainEntries

    function choose(entry) {
        if (!entry || entry.sep || entry.off)
            return
        if (entry.id === "signals")
            page = "signals"
        else
            picked(entry.id, entry.number || 0)
    }
    // Keyboard: arrows move over what can be chosen, Enter chooses.
    function move(step) {
        var list = currentEntries
        var i = cursor
        for (var n = 0; n < list.length; n++) {
            i = (i + step + list.length) % list.length
            if (cursor < 0 && step < 0 && n === 0)
                i = list.length - 1
            if (!list[i].sep && !list[i].off) {
                cursor = i
                return
            }
        }
    }
    function activate() { if (cursor >= 0) choose(currentEntries[cursor]) }
    function back() {
        if (page === "signals")
            page = "main"
        else
            dismissed()
    }

    Rectangle {
        anchors.fill: parent
        color: theme.scrim
        MouseArea { anchors.fill: parent; onClicked: root.dismissed() }
    }

    // A soft shadow from a few flat layers: the software renderer has no blur.
    component Shadow: Item {
        id: shadow
        property real strength: 0.6
        Repeater {
            model: 5
            Rectangle {
                required property int index
                readonly property real grow: 2 + index * 4
                x: -grow
                y: 12 - grow
                width: shadow.width + 2 * grow
                height: shadow.height + 2 * grow
                radius: Ui.menuRadius + grow
                color: Qt.rgba(0, 0, 0, shadow.strength * 0.13)
            }
        }
    }

    // ---- the lifted row
    Item {
        id: card
        visible: root.lifted
        x: root.rowX + 6
        y: Math.max(4, Math.min(root.rowY, root.height - height - 4))
        width: root.rowWidth - 12
        height: Math.max(root.rowHeight, 56)
        Shadow { anchors.fill: parent; strength: 0.7 }
        Rectangle {
            anchors.fill: parent
            color: theme.background
            radius: Ui.menuRadius
            border.width: 2
            border.color: theme.accent
        }
        Icon {
            id: cardGlyph
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: root.info.glyph || "app"
            color: Ui.glyphColor(root.info.glyph)
            size: 20
        }
        Column {
            anchors.left: cardGlyph.right
            anchors.leftMargin: 12
            anchors.right: cardValue.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            T { width: parent.width; text: root.info.name || ""; color: theme.accent; font.weight: Font.DemiBold }
            T {
                width: parent.width
                text: "PID " + (root.info.pid || "") + " · " + (root.info.user || "")
                      + (root.info.appName && root.info.appName !== root.info.name ? " · " + root.info.appName : "")
                color: theme.secondary
                font.pixelSize: theme.fontLabel
            }
        }
        Column {
            id: cardValue
            visible: root.value.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            T { anchors.right: parent.right; text: root.value; color: theme.bright; font.weight: Font.DemiBold }
            T { anchors.right: parent.right; text: root.valueTitle; color: theme.secondary; font.pixelSize: theme.fontCaption }
        }
        MouseArea { anchors.fill: parent; onClicked: root.dismissed() }
    }

    // ---- a menu box: an optional note, an optional icon row, entries
    component Box: Item {
        id: box
        property var entries: []
        property bool iconRow: false
        property string note
        property int cursor: -1
        property string openId       // the entry whose submenu is showing
        signal chosen(var entry)

        width: root.menuWidth
        height: Math.min(body.height + 8, root.height - 16)

        Shadow { anchors.fill: parent }
        Rectangle {
            anchors.fill: parent
            color: theme.background
            radius: Ui.menuRadius
            border.width: 1
            border.color: theme.menuBorder
        }
        MouseArea { anchors.fill: parent } // a tap between entries is not a dismissal
        Flickable {
            x: 1
            y: 4
            width: parent.width - 2
            height: parent.height - 8
            contentHeight: body.height
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            clip: interactive
            Column {
                id: body
                width: parent.width

                T {
                    visible: box.note.length > 0
                    width: parent.width
                    leftPadding: 14
                    rightPadding: 14
                    topPadding: 8
                    bottomPadding: 8
                    text: box.note
                    color: theme.yellow
                    font.pixelSize: theme.fontLabel
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Row {
                    id: icons
                    visible: box.iconRow
                    width: parent.width
                    height: visible ? 60 : 0
                    readonly property var items: {
                        var off = root.blocked.length > 0
                        var list = [{ id: "quit", text: "Quit", icon: "x-circle", off: off },
                                    { id: "forcequit", text: "Force Quit", icon: "x-circle", red: true, off: off },
                                    { id: "pause", text: root.paused ? "Resume" : "Pause", icon: root.paused ? "play" : "pause", off: off }]
                        if (root.canInspect)
                            list.push({ id: "inspect", text: "Inspect", icon: "info" })
                        return list
                    }
                    Repeater {
                        model: icons.items
                        Item {
                            id: iconButton
                            required property var modelData
                            readonly property color ink: modelData.off ? theme.secondary : modelData.red ? theme.red : theme.foreground
                            width: icons.width / icons.items.length
                            height: icons.height
                            opacity: modelData.off ? 0.6 : 1
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: Ui.controlRadius
                                color: iconMa.pressed && !iconButton.modelData.off ? theme.pressed : "transparent"
                            }
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Icon { name: iconButton.modelData.icon; color: iconButton.ink; size: 18; anchors.horizontalCenter: parent.horizontalCenter }
                                T {
                                    text: iconButton.modelData.text
                                    color: iconButton.ink
                                    font.pixelSize: theme.fontCaption
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                            MouseArea { id: iconMa; anchors.fill: parent; onClicked: box.chosen(iconButton.modelData) }
                        }
                    }
                }
                Item {
                    visible: box.iconRow
                    width: parent.width
                    height: visible ? 9 : 0
                    Rectangle { y: 4; width: parent.width; height: 1; color: theme.menuBorder }
                }

                // One touch area for all entries: the highlight follows a
                // finger that slides over them, and lifting it chooses.
                Item {
                    id: entryArea
                    width: parent.width
                    height: entryColumn.height
                    property int under: -1
                    property bool down: false
                    function indexAt(y) {
                        for (var i = 0; i < entryRepeater.count; i++) {
                            var it = entryRepeater.itemAt(i)
                            if (it && y >= it.y && y < it.y + it.height)
                                return box.entries[i].sep || box.entries[i].off ? -1 : i
                        }
                        return -1
                    }
                    Column {
                        id: entryColumn
                        width: parent.width
                        Repeater {
                            id: entryRepeater
                            model: box.entries
                            Item {
                                id: entry
                                required property var modelData
                                required property int index
                                width: entryColumn.width
                                height: modelData.sep ? 9 : 44
                                Rectangle { visible: entry.modelData.sep === true; y: 4; width: parent.width; height: 1; color: theme.menuBorder }
                                MenuRow {
                                    visible: !entry.modelData.sep
                                    anchors.fill: parent
                                    interactive: false
                                    enabled: !entry.modelData.off
                                    text: entry.modelData.text || ""
                                    hint: entry.modelData.hint || ""
                                    trailingIcon: entry.modelData.arrow ? "chevron-right" : ""
                                    tint: entry.modelData.red ? theme.red : theme.foreground
                                    highlighted: entryArea.under === entry.index || box.cursor === entry.index
                                                 || (box.openId.length > 0 && entry.modelData.id === box.openId)
                                    held: entryArea.down && entryArea.under === entry.index
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onPositionChanged: mouse => entryArea.under = entryArea.indexAt(mouse.y)
                        onPressed: mouse => { entryArea.under = entryArea.indexAt(mouse.y); entryArea.down = true }
                        onReleased: mouse => {
                            var inside = mouse.x >= 0 && mouse.x < width && mouse.y >= 0 && mouse.y < height
                            var i = inside ? entryArea.indexAt(mouse.y) : -1
                            entryArea.down = false
                            entryArea.under = -1
                            if (i >= 0)
                                box.chosen(box.entries[i])
                        }
                        onCanceled: { entryArea.down = false; entryArea.under = -1 }
                        onExited: if (!pressed) entryArea.under = -1
                    }
                }
            }
        }
    }

    function placeY(wanted, h) { return Math.round(Math.max(8, Math.min(wanted, height - 8 - h))) }

    Box {
        id: mainBox
        visible: root.page === "main" || root.sideBySide
        entries: root.mainEntries
        iconRow: root.narrow
        note: root.blocked
        cursor: root.page === "main" ? root.cursor : -1
        openId: root.page === "signals" ? "signals" : ""
        x: Math.round(root.lifted ? Math.min(card.x, root.width - 8 - width) : Math.max(8, Math.min(root.anchorRight - width, root.width - 8 - width)))
        // Under the row; above it when the row is too close to the bottom.
        y: {
            if (!root.lifted)
                return root.placeY(root.anchorY + 6, height)
            var below = card.y + card.height + 6
            if (below + height <= root.height - 8)
                return Math.round(below)
            var above = card.y - 6 - height
            return above >= 8 ? Math.round(above) : root.placeY(below, height)
        }
        onChosen: entry => root.choose(entry)
    }
    Box {
        id: signalBox
        visible: root.page === "signals"
        entries: root.signalEntries
        cursor: root.cursor
        x: root.sideBySide ? mainBox.x + mainBox.width + 4 : mainBox.x
        y: root.sideBySide ? root.placeY(mainBox.y + mainBox.height - 3 * 44 - 8, height) : root.placeY(mainBox.y, height)
        onChosen: entry => root.choose(entry)
    }
}
