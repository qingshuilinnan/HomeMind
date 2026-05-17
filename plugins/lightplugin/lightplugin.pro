QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = lightplugin

INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

HEADERS += \
    lightplugin.h \
    lightcontrol.h

SOURCES += \
    lightplugin.cpp \
    lightcontrol.cpp

FORMS += \
    lightcontrol.ui

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
