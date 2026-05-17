QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = acplugin

INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

HEADERS += \
    acplugin.h \
    accontrol.h

SOURCES += \
    acplugin.cpp \
    accontrol.cpp

FORMS += \
    accontrol.ui

# Host sources needed by the plugin
SOURCES += \
    $$PWD/../../database.cpp \
    $$PWD/../../devicediscovery.cpp \
    $$PWD/../../mqttclient.cpp

HEADERS += \
    $$PWD/../../database.h \
    $$PWD/../../devicediscovery.h \
    $$PWD/../../mqttclient.h

DESTDIR = $$OUT_PWD/..
