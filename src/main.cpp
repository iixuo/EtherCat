#include <QApplication>
#include "mainwindow.h"

// QT程序核心入口
int main(int argc, char *argv[])
{
    // 初始化QT应用程序（必须有，管理事件循环）
    QApplication a(argc, argv);

    // 创建主窗口对象
    MainWindow w;
    // 显示主窗口
    w.show();

    // 运行QT事件循环，阻塞直到窗口关闭
    return a.exec();
}
