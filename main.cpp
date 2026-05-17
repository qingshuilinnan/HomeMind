#include "widget.h"
#include "loginwindow.h"
#include "database.h"
#include "plugin/pluginmanager.h"
#include <QApplication>
#include <QDir>

#include <QFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置工作目录为程序所在目录
    QDir::setCurrent(QCoreApplication::applicationDirPath());

    // 加载全局样式表
    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        a.setStyleSheet(stream.readAll());
    }

    // 加载插件 (按优先级依次尝试多个路径)
    PluginManager *pm = PluginManager::instance();
    QStringList pluginPaths = {
        QCoreApplication::applicationDirPath() + "/plugins",
        QDir::currentPath() + "/plugins",
        QCoreApplication::applicationDirPath() + "/../plugins",
        QDir::currentPath() + "/../plugins"
    };
    for (const QString &path : pluginPaths) {
        if (QDir(path).exists()) {
            pm->loadPlugins(path);
            if (!pm->plugins().isEmpty()) break;
        }
    }

    Database db;
    QString autoUser = "";
    if (db.open()) {
        autoUser = db.getAutoLoginUser();
    }

    if (!autoUser.isEmpty()) {
        LoginWindow::currentUsername = autoUser;
        Widget *w = new Widget();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    } else {
        LoginWindow *loginWindow = new LoginWindow();
        loginWindow->setAttribute(Qt::WA_DeleteOnClose);

        QObject::connect(loginWindow, &LoginWindow::loginSuccess, [=]() {
            Widget *w = new Widget();
            w->setAttribute(Qt::WA_DeleteOnClose);
            w->show();
        });

        loginWindow->show();
    }

    return a.exec();
}
