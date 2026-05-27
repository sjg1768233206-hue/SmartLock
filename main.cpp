#include <QApplication>
#include "mainwindow.h"
#include <QDebug>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.resize(640, 480);  // 设置固定大小
    w.show();
    return a.exec();
}