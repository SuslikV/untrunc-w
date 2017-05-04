#-------------------------------------------------
#
# Project created by QtCreator 2012-10-28T12:50:54
#
#-------------------------------------------------

QT       -= core
QT       -= gui

TARGET = untrunc-w
CONFIG   += console
CONFIG   -= -qt app_bundle


TEMPLATE = app


SOURCES += main.cpp \
    atom.cpp \
    mp4.cpp \
    file.cpp \
    track.cpp \
    loginfo.cpp

HEADERS += \
    atom.h \
    mp4.h \
    file.h \
    track.h \
    portable_endian.h \
    portable_stdio.h \
    AP_AtomDefinitions.h \
    loginfo.h

#you can add dependencies from obsproject
INCLUDEPATH += $$PWD/../untrunc_deps/win32/include
#win+linux
LIBS += -L/usr/local/lib -L$$PWD/../untrunc_deps/win32/bin -lavformat -lavcodec -lavutil

DEFINES += _FILE_OFFSET_BITS=64

