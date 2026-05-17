QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = sensorplugin

INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

HEADERS += \
    sensorplugin.h \
    sensorcontrol.h

SOURCES += \
    sensorplugin.cpp \
    sensorcontrol.cpp

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
