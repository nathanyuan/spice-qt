#include <QtGui>
#include <QtCore>
#include "loginwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    LoginWindow loginwindow;
    loginwindow.show();
    return app.exec();
}

