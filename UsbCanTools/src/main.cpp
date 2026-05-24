// 文件说明：应用入口，负责创建 QApplication 与主窗口。
#include "ui/main_window.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/elecan_app_256.ico")));

    MainWindow window;
    window.setWindowIcon(QIcon(QStringLiteral(":/icons/elecan_app_256.ico")));
    window.showMaximized();

    return app.exec();
}
