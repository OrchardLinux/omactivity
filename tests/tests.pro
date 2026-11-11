TEMPLATE = app
TARGET = tst_omactivity
QT += testlib quick
CONFIG += c++17 testcase console
CONFIG -= app_bundle debug_and_release
DEFINES += FIXTURES=\\\"$$PWD/fixtures\\\" DATA_DIR=\\\"$$PWD/../data\\\"

include(../core.pri)

HEADERS += ../src/actions.h ../src/collector.h ../src/inspector.h ../src/painteditems.h ../src/prefs.h ../src/toplevels.h
SOURCES += ../src/actions.cpp ../src/collector.cpp ../src/inspector.cpp ../src/painteditems.cpp ../src/toplevels.cpp \
    tst_omactivity.cpp
# The interface test loads the app's own QML.
RESOURCES += ../omactivity.qrc
