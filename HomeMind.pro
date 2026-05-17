QT += widgets sql network

CONFIG += c++17

INCLUDEPATH += $$PWD/plugin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    widget.cpp \
    roomdetail.cpp \
    database.cpp \
    loginwindow.cpp \
    httpserver.cpp \
    devicediscovery.cpp \
    devicecommander.cpp \
    mqttclient.cpp \
    plugin/pluginmanager.cpp

HEADERS += \
    widget.h \
    roomdetail.h \
    database.h \
    loginwindow.h \
    httpserver.h \
    devicediscovery.h \
    devicecommander.h \
    mqttclient.h \
    plugin/deviceplugininterface.h \
    plugin/pluginmanager.h

FORMS += \
    loginwindow.ui \
    widget.ui \
    roomdetail.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
