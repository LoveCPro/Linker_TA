#include "mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置应用程序样式
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // 设置应用程序信息
    QApplication::setApplicationName("RobotArmController");
    QApplication::setOrganizationName("YourCompany");
    QApplication::setApplicationVersion("1.0.0");

    MainWindow w;
    w.show();
    return a.exec();
}
