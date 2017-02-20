#include <QtCore>
#include <QtGui>

class SpiceQt;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    LoginWindow(QWidget *parent = NULL);
    ~LoginWindow() {}

public slots:
    void connectSpice();
private:
    SpiceQt *_spiceWidget;
    QLineEdit *ipedit;
    QLineEdit *portedit;
};
