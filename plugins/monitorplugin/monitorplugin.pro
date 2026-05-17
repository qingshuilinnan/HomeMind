QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = monitorplugin

INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

HEADERS += \
    monitorplugin.h \
    monitorcontrol.h

SOURCES += \
    monitorplugin.cpp \
    monitorcontrol.cpp

SOURCES += \
    $$PWD/../../database.cpp \
    $$PWD/../../devicediscovery.cpp \
    $$PWD/../../mqttclient.cpp

HEADERS += \
    $$PWD/../../database.h \
    $$PWD/../../devicediscovery.h \
    $$PWD/../../mqttclient.h

DESTDIR = $$OUT_PWD/..
