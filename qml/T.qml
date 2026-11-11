import QtQuick

// Text in the theme's font. PlainText always: process names and command
// lines are data, never markup.
Text {
    textFormat: Text.PlainText
    color: theme.foreground
    font.family: theme.fontFamily
    font.pixelSize: theme.fontTitle
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
