TEMPLATE = lib

CXXFLAGS += -g
INCLUDEPATH += /usr/include/glib-2.0 /usr/lib64/glib-2.0/include /usr/include/spice-client-glib-2.0 /usr/include/pixman-1 /usr/include/spice-1 ./common ./headers
LIBS += `pkg-config --libs glib-2.0 spice-client-glib-2.0`
HEADERS += headers/spice-common.h \
           headers/spice-widget.h \
           headers/spice-widget-priv.h \
           headers/spice-gtk-session.h \
           headers/spiceqt.h

SOURCES += src/spice-widget.cpp \
           src/spiceqt.cpp
