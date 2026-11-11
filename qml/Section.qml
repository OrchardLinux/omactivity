import QtQuick

// "PROCESS" section heading: small, spaced capitals, nothing else.
Item {
    property string title
    implicitHeight: 32
    T {
        text: parent.title.toUpperCase()
        color: theme.secondary
        font.pixelSize: theme.fontLabel
        font.letterSpacing: 1.3
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
    }
}
