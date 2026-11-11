pragma Singleton
import QtQuick

// Sizes and small helpers shared by every component.
QtObject {
    // Sizes are plain pixels; sp() is where a scale would go.
    function sp(n) { return Math.round(n) }
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    readonly property int margin: 16
    readonly property int rowHeight: prefs.comfortable ? 60 : 48
    readonly property int headerHeight: 44
    // Visible controls are as tall as a touch target.
    readonly property int controlHeight: 44
    readonly property int line: 1
    // Corners follow Hyprland's rounding, capped per kind; square on labwc.
    readonly property int controlRadius: Math.min(4, theme.radius)
    readonly property int menuRadius: Math.min(6, theme.radius)
    readonly property int cardRadius: Math.min(10, theme.radius)

    function glyphColor(glyph) {
        switch (glyph) {
        case "globe": return theme.orange
        case "terminal": return theme.green
        case "folder": return theme.cyan
        case "calculator": return theme.magenta
        case "activity": return theme.accent
        case "cpu":
        case "layers": return theme.secondary
        }
        return theme.foreground
    }

    // Line icons on a 24 x 24 grid, drawn with a round 2 px stroke.
    readonly property var icons: ({
        "x": "M6 6 L18 18 M18 6 L6 18",
        "x-circle": "M3 12 a9 9 0 1 0 18 0 a9 9 0 1 0 -18 0 M9 9 L15 15 M15 9 L9 15",
        "info": "M3 12 a9 9 0 1 0 18 0 a9 9 0 1 0 -18 0 M12 11 V16 M12 8 V8.01",
        "chevron-right": "M9 6 L15 12 L9 18",
        "chevron-down": "M6 9 L12 15 L18 9",
        "search": "M4 11 a7 7 0 1 0 14 0 a7 7 0 1 0 -14 0 M16 16 L21 21",
        "list-tree": "M21 12 H13 M21 6 H8 M21 18 H13 M3 6 V10 a2 2 0 0 0 2 2 H8 M3 10 V16 a2 2 0 0 0 2 2 H8",
        "globe": "M3 12 a9 9 0 1 0 18 0 a9 9 0 1 0 -18 0 M3 12 H21 M12 3 C8.5 6.5 8.5 17.5 12 21 M12 3 C15.5 6.5 15.5 17.5 12 21",
        "cpu": "M7 5 H17 a2 2 0 0 1 2 2 V17 a2 2 0 0 1 -2 2 H7 a2 2 0 0 1 -2 -2 V7 a2 2 0 0 1 2 -2 Z M9 9 H15 V15 H9 Z M9 2 V5 M15 2 V5 M9 19 V22 M15 19 V22 M2 9 H5 M2 15 H5 M19 9 H22 M19 15 H22",
        "layers": "M12 3 L21 8 L12 13 L3 8 Z M3 12 L12 17 L21 12 M3 16 L12 21 L21 16",
        "terminal": "M5 4 H19 a2 2 0 0 1 2 2 V18 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 V6 a2 2 0 0 1 2 -2 Z M7 9 L10 12 L7 15 M12 15 H16",
        "activity": "M3 12 H7 L10 4 L14 20 L17 12 H21",
        "folder": "M3 6 a2 2 0 0 1 2 -2 H9 L11 7 H19 a2 2 0 0 1 2 2 V18 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 Z",
        "calculator": "M7 2 H17 a2 2 0 0 1 2 2 V20 a2 2 0 0 1 -2 2 H7 a2 2 0 0 1 -2 -2 V4 a2 2 0 0 1 2 -2 Z M8 6 H16 M8 11 V11.01 M12 11 V11.01 M16 11 V11.01 M8 15 V15.01 M12 15 V15.01 M16 15 V15.01 M8 18 V18.01 M12 18 V18.01 M16 18 V18.01",
        "pause": "M9 5 V19 M15 5 V19",
        "play": "M7 4 L20 12 L7 20 Z",
        "shield": "M12 3 L20 6 V11 C20 16 16.5 19.5 12 21 C7.5 19.5 4 16 4 11 V6 Z",
        "corner": "M5 5 V11 a3 3 0 0 0 3 3 H19 M15 10 L19 14 L15 18",
        "arrow-down": "M12 5 V19 M6 13 L12 19 L18 13",
        "arrow-up": "M12 19 V5 M6 11 L12 5 L18 11",
        "arrow-up-right": "M7 17 L17 7 M9 7 H17 V15",
        "more": "M5 12 V12.01 M12 12 V12.01 M19 12 V12.01",
        "alert": "M12 3 L22 20 H2 Z M12 10 V14 M12 17 V17.01",
        "memory": "M3 8 H21 V16 H3 Z M7 12 V12.01 M11 12 V12.01 M15 12 V12.01 M6 16 V19 M10 16 V19 M14 16 V19 M18 16 V19",
        "energy": "M13 2 L4 14 H12 L11 22 L20 10 H12 Z",
        "disk": "M3 13 L6 5 H18 L21 13 V18 a1 1 0 0 1 -1 1 H4 a1 1 0 0 1 -1 -1 Z M3 13 H21 M7 16 V16.01 M11 16 V16.01",
        "network": "M9 3 H15 V8 H9 Z M3 16 H9 V21 H3 Z M15 16 H21 V21 H15 Z M12 8 V12 M6 16 V12 H18 V16",
        "app": "M4 4 H20 a1 1 0 0 1 1 1 V19 a1 1 0 0 1 -1 1 H4 a1 1 0 0 1 -1 -1 V5 a1 1 0 0 1 1 -1 Z M3 9 H21 M6.5 6.5 V6.51 M9.5 6.5 V6.51",
        "check": "M5 12 L10 17 L19 7",
        "settings": "M4 7 H12 M20 7 H18 M4 17 H6 M12 17 H20 M14 7 a2 2 0 1 0 4 0 a2 2 0 1 0 -4 0 M6 17 a2 2 0 1 0 4 0 a2 2 0 1 0 -4 0",
        "file-text": "M6 3 H14 L19 8 V20 a1 1 0 0 1 -1 1 H6 a1 1 0 0 1 -1 -1 V4 a1 1 0 0 1 1 -1 Z M14 3 V8 H19 M9 13 H15 M9 17 H15",
        "message": "M4 5 H20 V17 H9 L5 21 V17 H4 Z",
        "gamepad": "M6 8 H18 a4 4 0 0 1 4 4 a4 4 0 0 1 -4 4 H6 a4 4 0 0 1 -4 -4 a4 4 0 0 1 4 -4 Z M7 11 V13 M6 12 H8 M16 12 V12.01",
        "image": "M4 4 H20 V20 H4 Z M4 16 L9 11 L14 16 L17 13 L20 16 M13.5 8.5 a1.5 1.5 0 1 0 3 0 a1.5 1.5 0 1 0 -3 0",
        "code": "M8 7 L3 12 L8 17 M16 7 L21 12 L16 17",
        "lock": "M6 11 H18 V20 H6 Z M8 11 V8 a4 4 0 0 1 8 0 V11",
        "square": "M5 5 H19 V19 H5 Z",
        "check-square": "M5 5 H19 V19 H5 Z M8 12 L11 15 L16 9"
    })
}
