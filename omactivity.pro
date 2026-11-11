TEMPLATE = app
TARGET = omactivity
VERSION = 0.1.0

QT += quick dbus
CONFIG += c++17 warn_on
# QML is compiled at build time, so nothing is parsed or compiled at start
# (the app has to open fast on a slow, nearly full device).
CONFIG += qtquickcompiler
CONFIG -= debug_and_release
DEFINES += OMACTIVITY_VERSION=\\\"$$VERSION\\\" QT_NO_CAST_FROM_ASCII_WARN

include(core.pri)

HEADERS += \
    src/actions.h \
    src/collector.h \
    src/inhibitors.h \
    src/inspector.h \
    src/painteditems.h \
    src/prefs.h \
    src/toplevels.h
SOURCES += \
    src/actions.cpp \
    src/collector.cpp \
    src/inhibitors.cpp \
    src/inspector.cpp \
    src/main.cpp \
    src/painteditems.cpp \
    src/toplevels.cpp

RESOURCES += omactivity.qrc

# Closing an app's windows goes through the compositor
# (wlr-foreign-toplevel-management). Built wherever libwayland is; without
# it Quit starts at SIGTERM.
WAYLAND_SCANNER = $$system(command -v wayland-scanner)
!isEmpty(WAYLAND_SCANNER):unix:!macx {
    DEFINES += HAVE_WAYLAND
    LIBS += -lwayland-client
    PROTOCOL = $$PWD/protocols/wlr-foreign-toplevel-management-unstable-v1.xml

    wl_header.input = PROTOCOL
    wl_header.output = ${QMAKE_FILE_BASE}-client-protocol.h
    wl_header.commands = $$WAYLAND_SCANNER client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
    wl_header.variable_out = HEADERS
    wl_header.CONFIG += no_link target_predeps
    wl_code.input = PROTOCOL
    wl_code.output = ${QMAKE_FILE_BASE}-protocol.c
    wl_code.commands = $$WAYLAND_SCANNER private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
    wl_code.variable_out = SOURCES
    QMAKE_EXTRA_COMPILERS += wl_header wl_code
}

target.path = /usr/bin
desktop.files = data/omactivity.desktop
desktop.path = /usr/share/applications
icon.files = data/omactivity.svg
icon.path = /usr/share/icons/hicolor/scalable/apps
protected.files = data/protected.conf
protected.path = /etc/omactivity
INSTALLS += target desktop icon protected
