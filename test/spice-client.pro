TEMPLATE = app

CXXFLAGS += -g
QMAKE_LFLAGS += -L../
INCLUDEPATH += /usr/include/glib-2.0 /usr/lib64/glib-2.0/include /usr/include/spice-client-glib-2.0 /usr/include/pixman-1 /usr/include/spice-1 ../common ../headers
LIBS += -lspice-qt
HEADERS += src/loginwindow.h

SOURCES += src/main.cpp \
           src/loginwindow.cpp
