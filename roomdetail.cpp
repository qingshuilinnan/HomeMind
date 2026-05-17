#include "roomdetail.h"
#include "ui_roomdetail.h"
#include "database.h"
#include "loginwindow.h"
#include "plugin/pluginmanager.h"
#include "plugin/deviceplugininterface.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QProcess>
#include <QDir>
#include <QCoreApplication>
#include <QThread>
#include <QFileInfo>

RoomDetail::RoomDetail(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RoomDetail)
{
    ui->setupUi(this);

    // 窗口基本属性
    setMinimumSize(500, 400);
    setAttribute(Qt::WA_DeleteOnClose);
}

RoomDetail::~RoomDetail()
{
    delete ui;
}

void RoomDetail::setRoomName(const QString &name)
{
    roomName = name;
    setWindowTitle(name + " - 房间详情");
    rebuildFromPlugins();
}

void RoomDetail::loadDevicesFromDatabase()
{
    Database db;
    if (!db.open()) return;

    int userId = db.getUserId(LoginWindow::currentUsername);
    if (userId == -1) return;

    // Clear all lists
    for (auto it = m_listPluginMap.begin(); it != m_listPluginMap.end(); ++it) {
        it.key()->clear();
    }

    // Load devices for each plugin type
    for (auto it = m_listPluginMap.begin(); it != m_listPluginMap.end(); ++it) {
        QListWidget *listWidget = it.key();
        DevicePluginInterface *plugin = it.value();
        QString deviceType = plugin->deviceType();

        QList<QString> devices = db.getDevicesByRoomAndType(userId, roomName, deviceType);
        for (const QString &deviceName : devices) {
            addDeviceItem(listWidget, deviceName, deviceType);
        }
        addAddDeviceItem(listWidget, deviceType);
    }
}

void RoomDetail::addDeviceItem(QListWidget *listWidget, const QString &deviceName, const QString &deviceType)
{
    QListWidgetItem *item = new QListWidgetItem();
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);

    QLabel *nameLabel = new QLabel(deviceName);
    nameLabel->setObjectName("deviceNameLabel");
    nameLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #1a1a2e; background: transparent; border: none;");

    QPushButton *deleteButton = new QPushButton("\xE2\x9C\x95");
    deleteButton->setObjectName("deleteDeviceButton");
    deleteButton->setFixedSize(28, 28);
    deleteButton->setStyleSheet(
        "QPushButton { background-color: transparent; color: #c0c4cc; border: none; border-radius: 14px; font-size: 14px; font-weight: bold; padding: 0; }"
        "QPushButton:hover { background-color: #fef0f0; color: #f56c6c; }"
    );

    layout->addWidget(nameLabel, 1);
    layout->addWidget(deleteButton);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setMinimumHeight(48);

    listWidget->addItem(item);
    listWidget->setItemWidget(item, widget);
    item->setSizeHint(QSize(0, 52));

    // Delete button handler
    connect(deleteButton, &QPushButton::clicked, this, [=]() {
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            if (userId != -1) {
                db.deleteDevice(userId, roomName, deviceName, deviceType);
            }
        }
        delete listWidget->takeItem(listWidget->row(item));
        emit devicesChanged();
    });
}

void RoomDetail::addAddDeviceItem(QListWidget *listWidget, const QString &deviceType)
{
    QListWidgetItem *item = new QListWidgetItem();
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);

    QLabel *label = new QLabel("+ 添加" + deviceType);
    label->setStyleSheet("color: #4facfe; font-weight: 600; font-size: 13px; background: transparent; border: none;");

    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();
    layout->setContentsMargins(0, 0, 0, 0);

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setMinimumHeight(46);

    listWidget->addItem(item);
    listWidget->setItemWidget(item, widget);
    item->setSizeHint(QSize(0, 50));
    item->setData(Qt::UserRole, "ADD_BUTTON");
    item->setData(Qt::UserRole + 1, deviceType);
}

void RoomDetail::showAddDeviceDialog(const QString &deviceType)
{
    DevicePluginInterface *plugin = PluginManager::instance()->pluginForType(deviceType);
    if (!plugin) {
        QMessageBox::warning(this, "错误", "未找到对应的设备插件: " + deviceType);
        return;
    }

    if (plugin->showAddDeviceDialog(this, roomName, LoginWindow::currentUsername)) {
        loadDevicesFromDatabase();
        emit devicesChanged();
    }
}

void RoomDetail::on_addDeviceButton_clicked()
{
    // 按钮已从 UI 移除，此槽函数不再使用
}

void RoomDetail::showEmptyState()
{
    ui->deviceTabWidget->setVisible(false);

    QVBoxLayout *emptyLayout = new QVBoxLayout(this);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(16);

    QLabel *iconLabel = new QLabel("\xF0\x9F\x94\x8C");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 48px; background: transparent;");

    QLabel *msgLabel = new QLabel("暂无设备插件");
    msgLabel->setAlignment(Qt::AlignCenter);
    msgLabel->setStyleSheet("font-size: 16px; color: #909399; font-weight: 600; background: transparent;");

    m_compileStatus = new QLabel("");
    m_compileStatus->setAlignment(Qt::AlignCenter);
    m_compileStatus->setStyleSheet("font-size: 12px; color: #67c23a; background: transparent;");
    m_compileStatus->setVisible(false);

    QPushButton *reloadBtn = new QPushButton("重新加载插件");
    reloadBtn->setFixedWidth(160);
    reloadBtn->setStyleSheet(
        "QPushButton { background-color: #409eff; color: white; border: none; border-radius: 8px; "
        "padding: 10px 20px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #66b1ff; }"
    );
    connect(reloadBtn, &QPushButton::clicked, this, &RoomDetail::rebuildFromPlugins);

    m_compileBtn = new QPushButton("编译插件");
    m_compileBtn->setFixedWidth(160);
    m_compileBtn->setStyleSheet(
        "QPushButton { background-color: #67c23a; color: white; border: none; border-radius: 8px; "
        "padding: 10px 20px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #85ce61; }"
        "QPushButton:disabled { background-color: #c0c4cc; }"
    );
    connect(m_compileBtn, &QPushButton::clicked, this, &RoomDetail::startCompilePlugins);

    emptyLayout->addWidget(iconLabel);
    emptyLayout->addWidget(msgLabel);
    emptyLayout->addSpacing(8);
    emptyLayout->addWidget(m_compileBtn, 0, Qt::AlignCenter);
    emptyLayout->addWidget(m_compileStatus, 0, Qt::AlignCenter);
    emptyLayout->addSpacing(4);
    emptyLayout->addWidget(reloadBtn, 0, Qt::AlignCenter);

    setLayout(emptyLayout);
}

void RoomDetail::startCompilePlugins()
{
    // 查找插件源码目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString srcPluginDir;
    QStringList searchPaths = {
        appDir + "/../plugins",
        appDir + "/../../plugins",
        QDir::currentPath() + "/../plugins",
        QDir::currentPath() + "/plugins"
    };
    for (const QString &path : searchPaths) {
        if (QDir(path).exists() && !QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            srcPluginDir = QDir::cleanPath(path);
            break;
        }
    }

    if (srcPluginDir.isEmpty()) {
        m_compileStatus->setText("未找到插件源码目录 (plugins/)");
        m_compileStatus->setStyleSheet("font-size: 12px; color: #f56c6c; background: transparent;");
        m_compileStatus->setVisible(true);
        return;
    }

    // 收集插件子目录
    QDir srcDir(srcPluginDir);
    QStringList pluginDirs = srcDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    m_compileQueue.clear();
    for (const QString &dir : pluginDirs) {
        QString proFile = srcDir.filePath(dir + "/" + dir + ".pro");
        if (QFile::exists(proFile)) {
            m_compileQueue.append(proFile);
        }
    }

    if (m_compileQueue.isEmpty()) {
        m_compileStatus->setText("未找到插件项目文件 (.pro)");
        m_compileStatus->setStyleSheet("font-size: 12px; color: #f56c6c; background: transparent;");
        m_compileStatus->setVisible(true);
        return;
    }

    m_compileBtn->setEnabled(false);
    m_compileBtn->setText("编译中...");
    m_compileStatus->setVisible(true);
    m_compileStatus->setStyleSheet("font-size: 12px; color: #909399; background: transparent;");
    m_compileStatus->setText(QString("正在编译插件 (0/%1)...").arg(m_compileQueue.size()));

    m_compileIndex = 0;
    compileNextPlugin();
}

void RoomDetail::compileNextPlugin()
{
    if (m_compileIndex >= m_compileQueue.size()) {
        // 全部编译完成
        m_compileBtn->setEnabled(true);
        m_compileBtn->setText("编译插件");
        m_compileStatus->setStyleSheet("font-size: 12px; color: #67c23a; background: transparent;");
        m_compileStatus->setText("编译完成，正在加载插件...");

        // 重新加载插件
        rebuildFromPlugins();
        return;
    }

    QString proFile = m_compileQueue[m_compileIndex];
    QString pluginName = QFileInfo(proFile).baseName();
    m_compileStatus->setText(QString("正在编译 %1 (%2/%3)...")
        .arg(pluginName).arg(m_compileIndex + 1).arg(m_compileQueue.size()));

    // 创建构建目录
    QString buildBase = QCoreApplication::applicationDirPath() + "/../build/plugins/" + pluginName;
    QDir().mkpath(buildBase);

    // 查找 qmake
    QString qmakePath;
    QStringList qmakeSearch = {
        QDir::homePath() + "/Qt/5.15.2/gcc_64/bin/qmake",
        QCoreApplication::applicationDirPath() + "/../../../Qt/5.15.2/gcc_64/bin/qmake",
        "/usr/bin/qmake",
        "/usr/local/bin/qmake"
    };
    for (const QString &p : qmakeSearch) {
        if (QFile::exists(p)) {
            qmakePath = p;
            break;
        }
    }

    if (qmakePath.isEmpty()) {
        QProcess whichProc;
        whichProc.start("which", {"qmake"});
        whichProc.waitForFinished(3000);
        if (whichProc.exitCode() == 0) {
            qmakePath = QString(whichProc.readAllStandardOutput().trimmed());
        }
    }

    if (qmakePath.isEmpty()) {
        m_compileStatus->setStyleSheet("font-size: 12px; color: #f56c6c; background: transparent;");
        m_compileStatus->setText("未找到 qmake，请确保已安装 Qt 开发环境");
        m_compileBtn->setEnabled(true);
        m_compileBtn->setText("编译插件");
        return;
    }

    // 运行 qmake
    m_currentBuildDir = buildBase;
    QProcess *qmakeProc = new QProcess(this);
    qmakeProc->setWorkingDirectory(buildBase);
    connect(qmakeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RoomDetail::onQmakeFinished);
    qmakeProc->start(qmakePath, {proFile});
}

void RoomDetail::onQmakeFinished(int exitCode, QProcess::ExitStatus)
{
    QProcess *proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    if (exitCode != 0) {
        m_compileStatus->setStyleSheet("font-size: 12px; color: #f56c6c; background: transparent;");
        m_compileStatus->setText("qmake 失败: " + QString(proc->readAllStandardError().trimmed()).left(100));
        m_compileBtn->setEnabled(true);
        m_compileBtn->setText("编译插件");
        proc->deleteLater();
        return;
    }
    proc->deleteLater();

    // qmake 成功，运行 make
    runMake(m_currentBuildDir);
}

void RoomDetail::runMake(const QString &buildDir)
{
    QProcess *makeProc = new QProcess(this);
    makeProc->setWorkingDirectory(buildDir);
    connect(makeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RoomDetail::onMakeFinished);
    makeProc->start("make", {"-j" + QString::number(QThread::idealThreadCount())});
}

void RoomDetail::onMakeFinished(int exitCode, QProcess::ExitStatus)
{
    QProcess *proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    if (exitCode != 0) {
        QString err = QString(proc->readAllStandardError()).trimmed();
        m_compileStatus->setStyleSheet("font-size: 12px; color: #f56c6c; background: transparent;");
        m_compileStatus->setText("编译失败: " + err.left(100));
        m_compileBtn->setEnabled(true);
        m_compileBtn->setText("编译插件");
    } else {
        m_compileIndex++;
        compileNextPlugin();
    }
    proc->deleteLater();
}

void RoomDetail::rebuildFromPlugins()
{
    // 清理旧的 empty state 布局（如果有）
    if (layout() && layout() != ui->verticalLayout) {
        QLayout *oldLayout = layout();
        while (oldLayout->count()) {
            QLayoutItem *item = oldLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        delete oldLayout;
    }

    // 清理旧的 tab 和映射
    m_listPluginMap.clear();
    ui->deviceTabWidget->clear();
    ui->deviceTabWidget->setVisible(true);

    // 重新扫描插件目录
    PluginManager *pm = PluginManager::instance();
    pm->reloadPlugins();
    QList<DevicePluginInterface*> plugins = pm->plugins();

    if (plugins.isEmpty()) {
        showEmptyState();
        return;
    }

    for (DevicePluginInterface *plugin : plugins) {
        QWidget *tab = new QWidget();
        QVBoxLayout *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 0, 0, 0);

        QListWidget *listWidget = new QListWidget(tab);
        listWidget->setProperty("deviceType", plugin->deviceType());
        tabLayout->addWidget(listWidget);

        ui->deviceTabWidget->addTab(tab, plugin->iconText() + " " + plugin->displayName());
        m_listPluginMap.insert(listWidget, plugin);

        // Device click handler
        connect(listWidget, &QListWidget::itemClicked, this, [this, listWidget, plugin](QListWidgetItem *item) {
            if (item->data(Qt::UserRole).toString() != "ADD_BUTTON") {
                QWidget *widget = listWidget->itemWidget(item);
                if (widget) {
                    QLabel *label = widget->findChild<QLabel*>("deviceNameLabel");
                    if (label) {
                        QWidget *controlWidget = plugin->createControlWidget(roomName, label->text());
                        controlWidget->setProperty("isDeviceControl", true);
                        controlWidget->show();
                    }
                }
            }
        });

        // Add device click handler
        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (item->data(Qt::UserRole).toString() == "ADD_BUTTON") {
                QString type = item->data(Qt::UserRole + 1).toString();
                showAddDeviceDialog(type);
            }
        });
    }

    loadDevicesFromDatabase();
}
