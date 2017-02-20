#include "loginwindow.h"
#include "spiceqt.h"

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(400, 200);
    QGridLayout *layout = new QGridLayout();
    setLayout(layout);
    QLabel *label1 = new QLabel(this);
    label1->setText("IP Address: ");
    ipedit = new QLineEdit(this);
    ipedit->setText("127.0.0.1");
    layout->addWidget(label1, 0, 0); 
    layout->addWidget(ipedit, 0, 1); 

    QLabel *label2 = new QLabel(this);
    label2->setText("Port:  ");
    portedit = new QLineEdit(this);
    portedit->setText("5900");
    layout->addWidget(label2, 1, 0); 
    layout->addWidget(portedit, 1, 1); 

    QPushButton *connBtn = new QPushButton(this);
    connBtn->setText("Connect");
    connBtn->setFixedSize(60, 30);
    layout->addWidget(connBtn, 2, 0); 

    connect(connBtn, SIGNAL(clicked()), this, SLOT(connectSpice()));
}

void LoginWindow::connectSpice()
{
    _spiceWidget = SpiceQt::getSpice();
    qDebug() << ipedit->text() << "  ,  " << portedit->text() << endl;
    qDebug() << _spiceWidget;
    _spiceWidget->show();
    _spiceWidget->connectToGuest(ipedit->text(), portedit->text());
}
