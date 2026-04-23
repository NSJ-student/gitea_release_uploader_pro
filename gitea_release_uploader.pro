QT += core gui network widgets

CONFIG += c++11

TARGET = gitea_release_uploader_pro
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui
