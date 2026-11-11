# The parts without an interface: shared by the app and the tests.
INCLUDEPATH += $$PWD/src
HEADERS += \
    $$PWD/src/procfs.h \
    $$PWD/src/sampler.h \
    $$PWD/src/apps.h \
    $$PWD/src/protectedlist.h \
    $$PWD/src/processmodel.h \
    $$PWD/src/systeminfo.h \
    $$PWD/src/theme.h
SOURCES += \
    $$PWD/src/procfs.cpp \
    $$PWD/src/sampler.cpp \
    $$PWD/src/apps.cpp \
    $$PWD/src/protectedlist.cpp \
    $$PWD/src/processmodel.cpp \
    $$PWD/src/systeminfo.cpp \
    $$PWD/src/theme.cpp
