#include "widget.h"
#include "ui_widget.h"
#include "roomdetail.h"
#include "database.h"
#include "loginwindow.h"
#include "devicecommander.h"
#include "devicediscovery.h"
#include "plugin/pluginmanager.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QApplication>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , timeTimer(new QTimer(this))
    , weatherTimer(new QTimer(this))
    , automationTimer(new QTimer(this))
    , networkManager(new QNetworkAccessManager(this))
    , webServer(new HttpServer(this, this))
    , sensorTimer(new QTimer(this))
    , m_discovery(new DeviceDiscovery(this))
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    webServer->start(8080);
    connect(webServer, &HttpServer::dataChanged, this, [=]() {
        loadAllDevicesFromDatabase();
        loadTasksFromDatabase();

        foreach (QWidget *widget, QApplication::topLevelWidgets()) {
            if (RoomDetail *rd = qobject_cast<RoomDetail*>(widget)) {
                rd->loadDevicesFromDatabase();
            } else if (widget->property("isDeviceControl").toBool()) {
                QMetaObject::invokeMethod(widget, "loadState", Qt::DirectConnection);
            }
        }
    });

    // Initialize host services and inject into plugins
    m_hostServices.db = nullptr; // will be set per-use
    m_hostServices.discovery = m_discovery;
    m_hostServices.currentUsername = LoginWindow::currentUsername;
    m_hostServices.sendCommand = &DeviceCommander::sendCommand;
    m_hostServices.fetchSensorDataAsync = &DeviceCommander::fetchSensorDataAsync;
    m_hostServices.cacheSensorData = &DeviceCommander::cacheSensorData;
    PluginManager::instance()->setHostServices(&m_hostServices);

    m_discovery->startBrokerDiscovery();
    connect(m_discovery, &DeviceDiscovery::deviceStateChanged, this, [this](const QString &deviceId, const QJsonObject &state) {
        if (state.contains("temperature")) {
            DeviceCommander::cacheSensorData(deviceId, state);
            Database db;
            if (db.open()) {
                int userId = db.getUserId(LoginWindow::currentUsername);
                if (userId != -1) {
                    pollSensorData();
                }
            }
        }
    });

    initUI();
    initConnections();
    // 显示当前登录的账号
    ui->usernameLabel->setText("当前登录: " + LoginWindow::currentUsername);
    
    // 检查是否为管理员用户，如果不是则隐藏用户管理按钮
    Database db;
    if (db.open()) {
        if (!db.isAdmin(LoginWindow::currentUsername)) {
            ui->userManagementButton->hide();
        }
    }
    
    // 加载上次保存的位置信息并获取天气
    if (db.open()) {
        QString lastLocation = db.getUserLocation(LoginWindow::currentUsername);
        if (!lastLocation.isEmpty()) {
            ui->locationButton->setText(lastLocation);
            fetchWeather(lastLocation);
        }
    }
    
    // 设置天气自动更新定时器 (每30分钟更新一次)
    connect(weatherTimer, &QTimer::timeout, this, &Widget::autoUpdateWeather);
    weatherTimer->start(30 * 60 * 1000); 

    // 设置自动化任务引擎定时器 (每 1 秒检查一次，确保触发实时性)
    connect(automationTimer, &QTimer::timeout, this, &Widget::processAutomations);
    automationTimer->start(1000); 

    // 设置传感器数据定时轮询 (每 30 秒向传感器设备请求一次温湿度数据)
    connect(sensorTimer, &QTimer::timeout, this, &Widget::pollSensorData);
    QTimer::singleShot(3000, this, &Widget::pollSensorData);
    sensorTimer->start(30000);
}

Widget::~Widget()
{
    delete ui;
    delete timeTimer;
}

void Widget::initUI()
{
    updateStatusBar();
    ui->tempHumidityLabel->setText("温湿度: --°C / --%"); // 温湿预留位置，使用占位符代替具体数值
    
    // 清空列表
    ui->roomListWidget->clear();
    ui->sceneListWidget->clear();
    ui->allDevicesListWidget->clear();
    
    // 从数据库加载数据
    loadRoomsFromDatabase();
    loadScenesFromDatabase();
    loadAllDevicesFromDatabase();
    loadTasksFromDatabase();

    // 初始化插件管理 Tab
    initPluginTab();
}

void Widget::initConnections()
{
    // 时间更新
    connect(timeTimer, &QTimer::timeout, this, &Widget::updateTime);
    timeTimer->start(1000);

    // 连接房间列表点击事件处理 (统一在构造函数中连接，避免重复)
    connect(ui->roomListWidget, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        QWidget *widget = ui->roomListWidget->itemWidget(item);
        if (widget) {
            QLabel *nameLabel = widget->findChild<QLabel*>("roomNameLabel");
            if (nameLabel) {
                RoomDetail *roomDetail = new RoomDetail();
                roomDetail->setRoomName(nameLabel->text());
                connect(roomDetail, &RoomDetail::devicesChanged, this, &Widget::loadAllDevicesFromDatabase);
                roomDetail->show();
            }
        }
    });

    connect(ui->sceneListWidget, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        QWidget *widget = ui->sceneListWidget->itemWidget(item);
        if (widget) {
            QHBoxLayout *layout = qobject_cast<QHBoxLayout*>(widget->layout());
            if (layout && layout->itemAt(0)) {
                QLabel *label = qobject_cast<QLabel*>(layout->itemAt(0)->widget());
                if (label && !label->text().contains("\xF0\x9F\x8E\xAD")) {
                    showSceneSettingsDialog(label->text());
                } else if (layout->itemAt(1)) {
                    QLabel *nameLabel = qobject_cast<QLabel*>(layout->itemAt(1)->widget());
                    if (nameLabel) showSceneSettingsDialog(nameLabel->text());
                }
            }
        }
    });
    
    // 连接任务列表点击信号，用于修改任务
    connect(ui->taskListWidget, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem *item) {
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            QList<Database::TaskInfo> tasks = db.getTasks(userId);
            int row = ui->taskListWidget->row(item);
            if (row < tasks.size()) {
                showTaskEditDialog(&tasks[row]);
            }
        }
    });
    
    // 连接添加情景按钮
    connect(ui->addSceneButton, &QPushButton::clicked, this, [=]() {
        bool ok;
        QString sceneName = QInputDialog::getText(this, "添加情景", "请输入情景名称:", QLineEdit::Normal, "", &ok);
        if (ok && !sceneName.isEmpty()) {
            // 保存到数据库
            Database db;
            if (db.open()) {
                int userId = db.getUserId(LoginWindow::currentUsername);
                if (userId != -1) {
                    if (db.createScene(userId, sceneName)) {
                        // 添加到列表
                        addSceneItem(sceneName);
                    } else {
                        QMessageBox::warning(this, "错误", "添加情景失败");
                    }
                }
            } else {
                QMessageBox::warning(this, "错误", "数据库连接失败");
            }
        }
    });
}

void Widget::loadRoomsFromDatabase()
{
    Database db;
    if (db.open()) {
        int userId = db.getUserId(LoginWindow::currentUsername);
        if (userId != -1) {
            QList<QString> rooms = db.getRooms(userId);
            foreach (const QString &roomName, rooms) {
                addRoomItem(roomName);
            }
        }
    }
}

void Widget::addRoomItem(const QString &roomName)
{
    QListWidgetItem *item = new QListWidgetItem();
    QWidget *widget = new QWidget();
    widget->setObjectName("roomCard");

    static const QStringList gradients = {
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4facfe, stop:1 #00c6fb)",
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #43e97b, stop:1 #38f9d7)",
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #fa709a, stop:1 #fee140)",
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #a18cd1, stop:1 #fbc2eb)",
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #fccb90, stop:1 #d57eeb)",
        "qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #667eea, stop:1 #764ba2)",
    };
    int colorIndex = qHash(roomName) % gradients.size();
    QString gradient = gradients.at(colorIndex);

    QVBoxLayout *mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget *cardWidget = new QWidget();
    cardWidget->setStyleSheet(QString(
        "background: %1; border-radius: 14px;"
    ).arg(gradient));

    QVBoxLayout *cardLayout = new QVBoxLayout(cardWidget);
    cardLayout->setContentsMargins(14, 14, 14, 14);
    cardLayout->setSpacing(0);

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(0);

    QPushButton *deleteButton = new QPushButton("\xE2\x9C\x95");
    deleteButton->setObjectName("deleteRoomButton");
    deleteButton->setToolTip("删除房间");
    deleteButton->setProperty("roomName", roomName);
    deleteButton->setFixedSize(26, 26);
    deleteButton->setStyleSheet(
        "QPushButton { background-color: rgba(255,255,255,0.3); color: rgba(255,255,255,0.9); border: none; border-radius: 13px; font-size: 12px; font-weight: bold; padding: 0; }"
        "QPushButton:hover { background-color: rgba(255,255,255,0.55); }"
    );
    topRow->addStretch();
    topRow->addWidget(deleteButton);

    QLabel *iconLabel = new QLabel("\xF0\x9F\x8F\xA0");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 32px; background: transparent; border: none;");

    QLabel *nameLabel = new QLabel(roomName);
    nameLabel->setObjectName("roomNameLabel");
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #ffffff; background: transparent; border: none;");

    cardLayout->addLayout(topRow);
    cardLayout->addStretch();
    cardLayout->addWidget(iconLabel, 0, Qt::AlignCenter);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(nameLabel, 0, Qt::AlignCenter);
    cardLayout->addStretch();

    mainLayout->addWidget(cardWidget);

    widget->setFixedHeight(140);
    widget->setAttribute(Qt::WA_TranslucentBackground);

    ui->roomListWidget->addItem(item);
    ui->roomListWidget->setItemWidget(item, widget);
    item->setSizeHint(QSize(260, 152));

    connect(deleteButton, &QPushButton::clicked, this, [=]() {
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            if (userId != -1) {
                db.deleteRoom(userId, roomName);
            }
        }
        int row = ui->roomListWidget->row(item);
        if (row != -1) {
            delete ui->roomListWidget->takeItem(row);
        }
    });
}

void Widget::loadAllDevicesFromDatabase()
{
    ui->allDevicesListWidget->clear();
    Database db;
    if (db.open()) {
        int userId = db.getUserId(LoginWindow::currentUsername);
        if (userId != -1) {
            QList<Database::DeviceInfo> devices = db.getAllDevices(userId);
            foreach (const Database::DeviceInfo &info, devices) {
                QString state = db.getDeviceState(userId, info.room, info.name);
                addAllDeviceItem(info.name, info.room, info.type, state);
            }
        }
    }
}

void Widget::addAllDeviceItem(const QString &deviceName, const QString &roomName, const QString &deviceType, const QString &state)
{
    QListWidgetItem *item = new QListWidgetItem();
    QWidget *widget = new QWidget();
    widget->setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout *outerLayout = new QVBoxLayout(widget);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QWidget *cardWidget = new QWidget();
    cardWidget->setStyleSheet(
        "QWidget { background-color: #ffffff; border: 1px solid #ebeef5; border-radius: 10px; }"
    );

    QHBoxLayout *layout = new QHBoxLayout(cardWidget);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);

    DevicePluginInterface *plugin = PluginManager::instance()->pluginForType(deviceType);
    QString icon = plugin ? plugin->iconText() : "\xF0\x9F\x92\xA1";

    QLabel *iconLabel = new QLabel(icon);
    iconLabel->setStyleSheet("font-size: 18px; background: transparent; border: none;");
    iconLabel->setFixedWidth(32);

    QLabel *nameLabel = new QLabel(deviceName);
    nameLabel->setObjectName("deviceNameLabel");
    nameLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #1a1a2e; background: transparent; border: none;");
    
    QLabel *infoLabel = new QLabel(QString("[%1] %2").arg(roomName, deviceType));
    infoLabel->setStyleSheet("color: #909399; font-size: 12px; background: transparent; border: none;");

    QLabel *stateLabel = new QLabel();
    stateLabel->setAlignment(Qt::AlignCenter);
    QString stateDisplay;
    QString stateBg;
    QString stateFg;
    if (plugin) {
        stateDisplay = plugin->stateDisplayText(state);
    } else {
        if (state.isEmpty() || state == "未设置") {
            stateDisplay = "\xE2\x9A\xAA 未知";
        } else if (state.startsWith("开启")) {
            stateDisplay = "\xE2\x9A\xAA 运行中";
        } else if (state == "关闭") {
            stateDisplay = "\xE2\x98\xAA 已关机";
        } else {
            stateDisplay = "\xE2\x9A\xAA " + state;
        }
    }
    if (state.isEmpty() || state == "未设置") {
        stateFg = "#909399";
        stateBg = "#f0f2f5";
    } else if (state.startsWith("开启")) {
        stateFg = "#67c23a";
        stateBg = "#f0f9eb";
    } else if (state == "关闭") {
        stateFg = "#909399";
        stateBg = "#f5f7fa";
    } else {
        stateFg = "#67c23a";
        stateBg = "#f0f9eb";
    }
    stateLabel->setText(stateDisplay);
    stateLabel->setStyleSheet(
        QString("font-size: 11px; font-weight: 500; color: %1; background-color: %2; border: 1px solid %1; border-radius: 8px; padding: 2px 8px;").arg(stateFg, stateBg)
    );

    layout->addWidget(iconLabel);
    layout->addWidget(nameLabel, 1);
    layout->addWidget(infoLabel);
    layout->addSpacing(8);
    layout->addWidget(stateLabel);

    outerLayout->addWidget(cardWidget);
    ui->allDevicesListWidget->addItem(item);
    ui->allDevicesListWidget->setItemWidget(item, widget);
    item->setSizeHint(QSize(0, 52));
}

void Widget::loadScenesFromDatabase()
{
    Database db;
    if (db.open()) {
        int userId = db.getUserId(LoginWindow::currentUsername);
        if (userId != -1) {
            QList<QString> scenes = db.getScenes(userId);
            foreach (const QString &sceneName, scenes) {
                addSceneItem(sceneName);
            }
        }
    }
}

void Widget::addSceneItem(const QString &sceneName)
{
    QListWidgetItem *item = new QListWidgetItem();
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);

    QLabel *iconLabel = new QLabel("\xF0\x9F\x8E\xAD");
    iconLabel->setStyleSheet("font-size: 18px; background: transparent; border: none;");
    iconLabel->setFixedWidth(32);

    QLabel *label = new QLabel(sceneName);
    label->setStyleSheet("font-size: 15px; font-weight: 600; color: #1a1a2e; background: transparent; border: none;");

    QPushButton *applyButton = new QPushButton("\xE2\x9C\x85 \xE5\xBA\x94\xE7\x94\xA8");
    QPushButton *editButton = new QPushButton("\xE2\x9C\x8F");
    QPushButton *deleteButton = new QPushButton("\xE2\x9C\x95");

    applyButton->setObjectName("applySceneButton");
    deleteButton->setObjectName("deleteSceneButton");

    applyButton->setStyleSheet(
        "QPushButton { background-color: #409eff; color: white; border-radius: 6px; padding: 5px 14px; font-size: 12px; font-weight: 600; border: none; }"
        "QPushButton:hover { background-color: #66b1ff; }"
    );
    editButton->setFixedSize(30, 30);
    editButton->setStyleSheet(
        "QPushButton { background-color: transparent; color: #909399; border: 1px solid #e4e7ed; border-radius: 6px; font-size: 14px; padding: 0; }"
        "QPushButton:hover { background-color: #f5f7fa; color: #409eff; border-color: #b3d8ff; }"
    );
    deleteButton->setFixedSize(30, 30);
    deleteButton->setStyleSheet(
        "QPushButton { background-color: transparent; color: #c0c4cc; border: 1px solid #e4e7ed; border-radius: 6px; font-size: 14px; padding: 0; }"
        "QPushButton:hover { background-color: #fef0f0; color: #f56c6c; border-color: #fbc4c4; }"
    );

    applyButton->setProperty("sceneName", sceneName);
    editButton->setProperty("sceneName", sceneName);
    deleteButton->setProperty("sceneName", sceneName);

    layout->addWidget(iconLabel);
    layout->addWidget(label, 1);
    layout->addWidget(applyButton);
    layout->addWidget(editButton);
    layout->addWidget(deleteButton);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(6);

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setMinimumHeight(52);

    ui->sceneListWidget->addItem(item);
    ui->sceneListWidget->setItemWidget(item, widget);
    item->setSizeHint(QSize(0, 56)); 
    
    // 连接应用按钮
    connect(applyButton, &QPushButton::clicked, this, [=]() {
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            int sceneId = db.getSceneId(userId, sceneName);
            QList<Database::SceneDeviceInfo> devices = db.getSceneDevices(sceneId);
            
            if (devices.isEmpty()) {
                QMessageBox::information(this, "提示", QString("情景“%1”内未配置任何设备").arg(sceneName));
                return;
            }

            int count = 0;
            for (const auto &d : devices) {
                if (DeviceCommander::applyDeviceState(userId, d.roomName, d.deviceName, d.deviceType, d.state)) {
                    count++;
                }
            }
            QMessageBox::information(this, "应用成功", QString("情景\u201c%1\u201d已应用，共更新 %2 个设备状态").arg(sceneName).arg(count));
            loadAllDevicesFromDatabase();
            emit webServer->dataChanged();
        }
    });

    // 连接修改按钮
    connect(editButton, &QPushButton::clicked, this, [=]() {
        QWidget *widget = ui->sceneListWidget->itemWidget(item);
        if (widget) {
            QHBoxLayout *layout = qobject_cast<QHBoxLayout*>(widget->layout());
            if (layout && layout->itemAt(0)) {
                QLabel *label = qobject_cast<QLabel*>(layout->itemAt(0)->widget());
                if (label) {
                    bool ok;
                    QString oldName = label->text();
                    QString newName = QInputDialog::getText(this, "修改情景", "请输入新的情景名称:", QLineEdit::Normal, oldName, &ok);
                    if (ok && !newName.isEmpty() && newName != oldName) {
                        // 更新数据库
                        Database db;
                        if (db.open()) {
                            int userId = db.getUserId(LoginWindow::currentUsername);
                            if (userId != -1) {
                                if (db.updateScene(userId, oldName, newName)) {
                                    label->setText(newName);
                                } else {
                                    QMessageBox::warning(this, "错误", "修改情景失败");
                                }
                            }
                        } else {
                            QMessageBox::warning(this, "错误", "数据库连接失败");
                        }
                    }
                }
            }
        }
    });
    
    // 连接删除按钮
            connect(deleteButton, &QPushButton::clicked, this, [=]() {
                QWidget *widget = ui->sceneListWidget->itemWidget(item);
                if (widget) {
                    QHBoxLayout *layout = qobject_cast<QHBoxLayout*>(widget->layout());
                    if (layout && layout->itemAt(0)) {
                        QLabel *label = qobject_cast<QLabel*>(layout->itemAt(0)->widget());
                        if (label) {
                            QString sceneName = label->text();
                            // 从数据库删除
                            Database db;
                            if (db.open()) {
                                int userId = db.getUserId(LoginWindow::currentUsername);
                                if (userId != -1) {
                                    db.deleteScene(userId, sceneName);
                                }
                            }
                            // 从列表删除
                            delete ui->sceneListWidget->takeItem(ui->sceneListWidget->row(item));
                        }
                    }
                }
            });
}

void Widget::showSceneSettingsDialog(const QString &sceneName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("设置情景: " + sceneName);
    dialog.resize(600, 500);
    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    QLabel *tipLabel = new QLabel("点击设备进行设置，点击“保存情景”生效:");
    mainLayout->addWidget(tipLabel);

    QListWidget *deviceListWidget = new QListWidget(&dialog);
    mainLayout->addWidget(deviceListWidget);

    Database db;
    if (!db.open()) return;
    int userId = db.getUserId(LoginWindow::currentUsername);
    int sceneId = db.getSceneId(userId, sceneName);

    // 加载用户的所有设备
    QList<Database::DeviceInfo> allDevices = db.getAllDevices(userId);
    // 本地暂存当前情景的设置，点击保存后再写入数据库
    QList<Database::SceneDeviceInfo> currentSettings = db.getSceneDevices(sceneId);

    auto updateList = [&]() {
        deviceListWidget->clear();
        for (const auto &device : allDevices) {
            if (device.type == "传感器") continue;
            QListWidgetItem *item = new QListWidgetItem();
            QWidget *widget = new QWidget();
            QHBoxLayout *layout = new QHBoxLayout(widget);
            
            QString stateStr = "未设置";
            for (const auto &sd : currentSettings) {
                if (sd.deviceName == device.name && sd.roomName == device.room) {
                    stateStr = sd.state;
                    break;
                }
            }

            QLabel *nameLabel = new QLabel(QString("%1 (%2)").arg(device.name, device.room));
            QLabel *statusLabel = new QLabel(stateStr);
            statusLabel->setStyleSheet(stateStr == "未设置" ? "color: gray;" : "color: #409eff; font-weight: bold;");
            
            layout->addWidget(nameLabel);
            layout->addStretch();
            layout->addWidget(statusLabel);
            layout->setContentsMargins(10, 5, 10, 5);
            
            deviceListWidget->addItem(item);
            deviceListWidget->setItemWidget(item, widget);
            item->setSizeHint(QSize(widget->sizeHint().width(), 50));
            
            item->setData(Qt::UserRole, device.name);
            item->setData(Qt::UserRole + 1, device.type);
            item->setData(Qt::UserRole + 2, device.room);
            item->setData(Qt::UserRole + 3, stateStr);
        }
    };

    updateList();

    connect(deviceListWidget, &QListWidget::itemClicked, this, [=, &dialog, &currentSettings, &updateList](QListWidgetItem *item) {
        QString devName = item->data(Qt::UserRole).toString();
        QString devType = item->data(Qt::UserRole + 1).toString();
        QString roomName = item->data(Qt::UserRole + 2).toString();

        QDialog setDialog(&dialog);
        setDialog.setWindowTitle("设置设备状态: " + devName);
        QVBoxLayout *setLayout = new QVBoxLayout(&setDialog);

        QString newState;
        DevicePluginInterface *plugin = PluginManager::instance()->pluginForType(devType);
        if (plugin) {
            QWidget *editor = plugin->createSceneStateEditor(&setDialog);
            if (editor) {
                setLayout->addWidget(editor);
                QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &setDialog);
                setLayout->addWidget(bb);
                connect(bb, &QDialogButtonBox::accepted, &setDialog, &QDialog::accept);
                connect(bb, &QDialogButtonBox::rejected, &setDialog, &QDialog::reject);

                if (setDialog.exec() == QDialog::Accepted) {
                    newState = plugin->sceneEditorResult(editor);
                }
            } else {
                // Plugin doesn't support scene editing (e.g. sensors)
                bool ok;
                newState = QInputDialog::getText(&dialog, "设置状态", "请输入设备状态:", QLineEdit::Normal, "", &ok);
                if (!ok) return;
            }
        } else {
            bool ok;
            newState = QInputDialog::getText(&dialog, "设置状态", "请输入设备状态:", QLineEdit::Normal, "", &ok);
            if (!ok) return;
        }

        if (!newState.isEmpty()) {
            bool found = false;
            for (int i = 0; i < currentSettings.size(); ++i) {
                if (currentSettings[i].deviceName == devName && currentSettings[i].roomName == roomName) {
                    currentSettings[i].state = newState;
                    found = true;
                    break;
                }
            }
            if (!found) {
                Database::SceneDeviceInfo info;
                info.deviceName = devName;
                info.deviceType = devType;
                info.roomName = roomName;
                info.state = newState;
                currentSettings.append(info);
            }
            updateList();
        }
    });

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *saveBtn = new QPushButton("保存情景", &dialog);
    saveBtn->setStyleSheet("background-color: #67c23a; color: white; font-weight: bold;");
    QPushButton *cancelBtn = new QPushButton("取消", &dialog);
    
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(saveBtn, &QPushButton::clicked, this, [=, &dialog, &db, &currentSettings]() mutable {
        // 先清空该情景原有设置（或者由 addDeviceToScene 的 REPLACE 处理，但批量操作建议先处理）
        // 这里采用 REPLACE 逻辑，逐个写入
        bool success = true;
        for (const auto &info : currentSettings) {
            if (!db.addDeviceToScene(sceneId, info)) {
                success = false;
                break;
            }
        }
        if (success) {
            QMessageBox::information(&dialog, "成功", "情景配置已保存");
            dialog.accept();
        } else {
            QMessageBox::warning(&dialog, "错误", "保存失败，请检查数据库连接");
        }
    });

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void Widget::on_deleteRoomButton_clicked()
{
    // 此槽函数留作备用
}

void Widget::on_changePasswordButton_clicked()
{
    // 创建修改密码对话框
    QDialog dialog(this);
    dialog.setWindowTitle("修改密码");
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    QLabel *oldPasswordLabel = new QLabel("旧密码:", &dialog);
    layout->addWidget(oldPasswordLabel);
    
    QLineEdit *oldPasswordEdit = new QLineEdit(&dialog);
    oldPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(oldPasswordEdit);
    
    QLabel *newPasswordLabel = new QLabel("新密码:", &dialog);
    layout->addWidget(newPasswordLabel);
    
    QLineEdit *newPasswordEdit = new QLineEdit(&dialog);
    newPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(newPasswordEdit);
    
    QLabel *confirmPasswordLabel = new QLabel("确认新密码:", &dialog);
    layout->addWidget(confirmPasswordLabel);
    
    QLineEdit *confirmPasswordEdit = new QLineEdit(&dialog);
    confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(confirmPasswordEdit);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString oldPassword = oldPasswordEdit->text();
        QString newPassword = newPasswordEdit->text();
        QString confirmPassword = confirmPasswordEdit->text();
        
        if (oldPassword.isEmpty() || newPassword.isEmpty() || confirmPassword.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入所有密码字段");
            return;
        }
        
        if (newPassword != confirmPassword) {
            QMessageBox::warning(this, "错误", "两次输入的新密码不一致");
            return;
        }
        
        // 获取当前登录的用户名
        QString username = LoginWindow::currentUsername;
        if (username.isEmpty()) {
            QMessageBox::warning(this, "错误", "用户未登录");
            return;
        }
        
        // 验证旧密码并更新新密码
        Database db;
        if (db.open()) {
            // 首先验证旧密码是否正确
            if (db.loginUser(username, oldPassword)) {
                // 更新密码
                if (db.resetPassword(username, newPassword)) {
                    QMessageBox::information(this, "成功", "密码修改成功");
                } else {
                    QMessageBox::warning(this, "错误", "密码修改失败");
                }
            } else {
                QMessageBox::warning(this, "错误", "旧密码错误");
            }
        } else {
            QMessageBox::warning(this, "错误", "数据库连接失败");
        }
    }
}

void Widget::on_bindPhoneButton_clicked()
{
    // 绑定手机号功能暂不实现
    QMessageBox::information(this, "提示", "绑定手机号功能暂未实现");
}

void Widget::on_locationButton_clicked()
{
    // 防止频繁点击请求
    if (lastRequestTime.isValid() && lastRequestTime.secsTo(QDateTime::currentDateTime()) < 5) {
        QMessageBox::warning(this, "提示", "请求太频繁，请稍后再试");
        return;
    }
    
    ui->locationButton->setEnabled(false);
    ui->locationButton->setText("定位中...");
    lastRequestTime = QDateTime::currentDateTime();
    fetchLocation();
}

void Widget::fetchLocation()
{
    // 切换到 HTTPS 版本的 API
    QUrl url("https://ipapi.co/json/");
    QNetworkRequest request(url);
    
    // 增加 User-Agent 提高成功率
    request.setHeader(QNetworkRequest::UserAgentHeader, "HomeMind/1.0 (Qt5)");
    
    // 解决 SSL 握手失败问题：如果系统 OpenSSL 库版本不兼容，尝试忽略 SSL 错误
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(conf);
    
    QNetworkReply *reply = networkManager->get(request);
    
    // 解决 SSL 握手失败问题：使用 Lambda 明确调用 ignoreSslErrors() 避免函数重载歧义
    connect(reply, &QNetworkReply::sslErrors, [reply](const QList<QSslError> &errors) {
        Q_UNUSED(errors);
        reply->ignoreSslErrors();
    });
    
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            
            // ipapi.co 返回字段略有不同
            if (obj.contains("city")) {
                QString city = obj.value("city").toString();
                ui->locationButton->setText(city);
                
                // 定位成功后保存到数据库
                Database db;
                if (db.open()) {
                    db.saveUserLocation(LoginWindow::currentUsername, city);
                }
                
                fetchWeather(city);
            } else {
                QMessageBox::warning(this, "定位失败", "无法获取当前位置信息");
                ui->locationButton->setText("定位");
            }
        } else {
            qDebug() << "Location error:" << reply->errorString();
            QMessageBox::warning(this, "网络错误", "无法连接到定位服务 (HTTPS): " + reply->errorString());
            ui->locationButton->setText("定位");
        }
        ui->locationButton->setEnabled(true);
        reply->deleteLater();
    });
}

void Widget::autoUpdateWeather()
{
    Database db;
    if (db.open()) {
        QString lastLocation = db.getUserLocation(LoginWindow::currentUsername);
        if (!lastLocation.isEmpty()) {
            fetchWeather(lastLocation);
        }
    }
}

void Widget::fetchWeather(const QString &city)
{
    // 切换到 HTTPS 版本的 wttr.in
    QUrl url(QString("https://wttr.in/%1?format=j1&lang=zh").arg(city));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "HomeMind/1.0 (Qt5)");
    
    // 解决 SSL 握手失败问题
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(conf);
    
    QNetworkReply *reply = networkManager->get(request);
    
    // 解决 SSL 握手失败问题：使用 Lambda 明确调用 ignoreSslErrors() 避免函数重载歧义
    connect(reply, &QNetworkReply::sslErrors, [reply](const QList<QSslError> &errors) {
        Q_UNUSED(errors);
        reply->ignoreSslErrors();
    });
    
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            
            // 解析 wttr.in 的 JSON
            // current_condition 是一个数组
            QJsonArray conditions = obj.value("current_condition").toArray();
            if (!conditions.isEmpty()) {
                QJsonObject current = conditions.at(0).toObject();
                
                // 天气描述 (中文通常在 lang_zh 字段，但 wttr.in 默认返回英文，或者根据请求头)
                // 这里我们简化处理，取 temp_C 和 weatherDesc
                QString temp = current.value("temp_C").toString();
                
                // 获取中文描述
                QString desc = "";
                QJsonArray descArray = current.value("lang_zh").toArray();
                if (descArray.isEmpty()) {
                    descArray = current.value("weatherDesc").toArray();
                }
                
                if (!descArray.isEmpty()) {
                    desc = descArray.at(0).toObject().value("value").toString();
                }
                
                ui->weatherLabel->setText("天气: " + desc);
                ui->outdoorTempLabel->setText("室外温度: " + temp + "°C");
                
                // 更新内部存储
                this->weatherDesc = desc;
                this->outdoorTemp = temp;
            }
        }
        reply->deleteLater();
    });
}

void Widget::on_checkUpdateButton_clicked()
{
    // 模拟检查更新逻辑
    QMessageBox::information(this, "检查更新", "当前已是最新版本 (v1.0.2)");
}

void Widget::on_addTaskButton_clicked()
{
    showTaskEditDialog(nullptr);
}

void Widget::showTaskEditDialog(Database::TaskInfo *existingTask)
{
    QDialog dialog(this);
    dialog.setWindowTitle(existingTask ? "修改自动化任务" : "添加自动化任务");
    dialog.setMinimumWidth(400);
    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // --- 触发条件部分 ---
    QGroupBox *triggerGroup = new QGroupBox("1. 选择触发条件", &dialog);
    QVBoxLayout *triggerLayout = new QVBoxLayout(triggerGroup);
    
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QLabel *typeLabel = new QLabel("条件类型:", triggerGroup);
    QComboBox *typeCombo = new QComboBox(triggerGroup);
    typeCombo->addItems({"时间", "室内温度", "室外温度"});
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(typeCombo);
    triggerLayout->addLayout(typeLayout);

    // 条件值输入 (根据类型变化)
    QHBoxLayout *valLayout = new QHBoxLayout();
    QLabel *valLabel = new QLabel("设置数值:", triggerGroup);
    QLineEdit *valEdit = new QLineEdit(triggerGroup);
    valEdit->setPlaceholderText("例如: >26 或 <18");
    
    QTimeEdit *timeEdit = new QTimeEdit(QTime::currentTime(), triggerGroup);
    timeEdit->setDisplayFormat("HH:mm");
    timeEdit->setCalendarPopup(true);
    timeEdit->hide(); // 初始默认是“时间”，稍后根据类型显示
    
    valLayout->addWidget(valLabel);
    valLayout->addWidget(valEdit);
    valLayout->addWidget(timeEdit);
    triggerLayout->addLayout(valLayout);
    
    // 根据条件类型动态切换输入控件
    auto updateTriggerUI = [=](const QString &text) {
        if (text == "时间") {
            valEdit->hide();
            timeEdit->show();
            valLabel->setText("选择时间:");
        } else {
            valEdit->show();
            timeEdit->hide();
            valLabel->setText("设置数值:");
        }
    };
    connect(typeCombo, &QComboBox::currentTextChanged, updateTriggerUI);
    
    mainLayout->addWidget(triggerGroup);

    // --- 触发内容部分 ---
    QGroupBox *actionGroup = new QGroupBox("2. 选择执行动作", &dialog);
    QVBoxLayout *actionLayout = new QVBoxLayout(actionGroup);

    QHBoxLayout *actTypeLayout = new QHBoxLayout();
    QLabel *actTypeLabel = new QLabel("动作类型:", actionGroup);
    QComboBox *actTypeCombo = new QComboBox(actionGroup);
    actTypeCombo->addItems({"应用场景", "控制设备"});
    actTypeLayout->addWidget(actTypeLabel);
    actTypeLayout->addWidget(actTypeCombo);
    actionLayout->addLayout(actTypeLayout);

    // 目标选择 (场景或设备)
    QHBoxLayout *targetLayout = new QHBoxLayout();
    QLabel *targetLabel = new QLabel("目标对象:", actionGroup);
    QComboBox *targetCombo = new QComboBox(actionGroup);
    targetLayout->addWidget(targetLabel);
    targetLayout->addWidget(targetCombo);
    actionLayout->addLayout(targetLayout);

    // 动作值 (例如 "打开" 或 "26°C")
    QHBoxLayout *actValLayout = new QHBoxLayout();
    QLabel *actValLabel = new QLabel("执行细节:", actionGroup);
    QLineEdit *actValEdit = new QLineEdit(actionGroup);
    actValEdit->setPlaceholderText("例如: 应用, 打开, 26°C");
    actValLayout->addWidget(actValLabel);
    actValLayout->addWidget(actValEdit);
    actionLayout->addLayout(actValLayout);

    mainLayout->addWidget(actionGroup);

    // 动态加载目标对象列表
    auto updateTargets = [=](int index) {
        targetCombo->clear();
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            if (index == 0) { // 应用场景
                QStringList scenes = db.getScenes(userId);
                targetCombo->addItems(scenes);
            } else { // 控制设备
                QList<Database::DeviceInfo> devices = db.getAllDevices(userId);
                foreach(const auto &d, devices) {
                    targetCombo->addItem(QString("%1 (%2)").arg(d.name, d.room));
                }
            }
        }
    };
    connect(actTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateTargets);
    
    // 如果是修改模式，填充旧数据
    if (existingTask) {
        typeCombo->setCurrentText(existingTask->trigger_type);
        updateTriggerUI(existingTask->trigger_type);
        if (existingTask->trigger_type == "时间") {
            timeEdit->setTime(QTime::fromString(existingTask->trigger_value, "HH:mm"));
        } else {
            valEdit->setText(existingTask->trigger_value);
        }
        actTypeCombo->setCurrentText(existingTask->action_type);
        updateTargets(actTypeCombo->currentIndex());
        targetCombo->setCurrentText(existingTask->action_target);
        actValEdit->setText(existingTask->action_value);
    } else {
        updateTriggerUI("时间"); // 初始调用
        updateTargets(0); // 初始加载
    }

    // --- 确认按钮 ---
    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    mainLayout->addWidget(btnBox);
    connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        Database::TaskInfo info;
        if (existingTask) info.id = existingTask->id;
        info.trigger_type = typeCombo->currentText();
        
        if (info.trigger_type == "时间") {
            info.trigger_value = timeEdit->time().toString("HH:mm");
        } else {
            info.trigger_value = valEdit->text().trimmed();
        }
        
        info.action_type = actTypeCombo->currentText();
        info.action_target = targetCombo->currentText();
        info.action_value = actValEdit->text().trimmed();
        
        if (info.trigger_value.isEmpty() || info.action_target.isEmpty()) {
            QMessageBox::warning(this, "错误", "请填写完整的触发条件和目标对象");
            return;
        }

        info.content = QString("当[%1 %2]时 -> %3[%4] %5")
            .arg(info.trigger_type, info.trigger_value, info.action_type, info.action_target, info.action_value);
        info.completed = false;

        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            bool success = existingTask ? db.updateTask(info) : db.createTask(userId, info);
            if (success) {
                loadTasksFromDatabase();
            }
        }
    }
}

void Widget::loadTasksFromDatabase()
{
    ui->taskListWidget->clear();
    Database db;
    if (db.open()) {
        int userId = db.getUserId(LoginWindow::currentUsername);
        if (userId != -1) {
            QList<Database::TaskInfo> tasks = db.getTasks(userId);
            for (const auto &task : tasks) {
                addTaskItem(task);
            }
        }
    }
}

void Widget::addTaskItem(const Database::TaskInfo &task)
{
    QListWidgetItem *item = new QListWidgetItem(ui->taskListWidget);
    QWidget *widget = new QWidget();
    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setStyleSheet(
        "QWidget { background-color: #ffffff; border: 1px solid #ebeef5; border-radius: 10px; }"
    );

    QVBoxLayout *cardLayout = new QVBoxLayout(widget);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(10);

    QCheckBox *checkBox = new QCheckBox();
    checkBox->setChecked(task.completed);
    checkBox->setFixedSize(22, 22);
    checkBox->setCursor(Qt::PointingHandCursor);
    checkBox->setStyleSheet(
        "QCheckBox::indicator { width: 20px; height: 20px; border: 2px solid #dcdfe6; border-radius: 4px; background: white; }"
        "QCheckBox::indicator:hover { border-color: #4facfe; }"
        "QCheckBox::indicator:checked { background-color: #4facfe; border: 2px solid #4facfe; }"
    );

    QString triggerIcon;
    if (task.trigger_type == "\xE6\x97\xB6\xE9\x97\xB4") triggerIcon = "\xE2\x8F\xB0 ";
    else if (task.trigger_type.contains("\xE6\xB8\xA9\xE5\xBA\xA6")) triggerIcon = "\xF0\x9F\x90\xA1 ";
    else triggerIcon = "\xE2\x9A\xA1 ";

    QLabel *triggerLabel = new QLabel(triggerIcon + task.trigger_type);
    triggerLabel->setStyleSheet(
        "font-size: 11px; font-weight: 600; color: #4facfe; background-color: #f0f7ff; "
        "border: 1px solid #d4e8ff; border-radius: 8px; padding: 2px 8px;"
    );
    triggerLabel->setFixedHeight(22);
    triggerLabel->setAlignment(Qt::AlignCenter);

    topRow->addWidget(checkBox);
    topRow->addWidget(triggerLabel);
    topRow->addStretch();

    QPushButton *deleteBtn = new QPushButton("\xE2\x9C\x95");
    deleteBtn->setFixedSize(26, 26);
    deleteBtn->setCursor(Qt::PointingHandCursor);
    deleteBtn->setStyleSheet(
        "QPushButton { background-color: transparent; color: #c0c4cc; border: none; border-radius: 13px; font-size: 12px; font-weight: bold; padding: 0; }"
        "QPushButton:hover { background-color: #fef0f0; color: #f56c6c; }"
    );
    topRow->addWidget(deleteBtn);

    QLabel *contentLabel = new QLabel(task.content);
    contentLabel->setWordWrap(true);
    contentLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    contentLabel->setCursor(Qt::PointingHandCursor);
    contentLabel->setStyleSheet(task.completed ?
        "color: #909399; text-decoration: line-through; font-size: 13px; font-weight: 500; background: transparent;" :
        "color: #303133; font-size: 13px; font-weight: 500; background: transparent;");

    cardLayout->addLayout(topRow);
    cardLayout->addWidget(contentLabel);

    ui->taskListWidget->setItemWidget(item, widget);

    QFontMetrics fm(QFont("Microsoft YaHei", 13));
    int availW = qMax(ui->taskListWidget->viewport()->width() - 60, 200);
    QRect textRect = fm.boundingRect(QRect(0, 0, availW, 5000), Qt::TextWordWrap, task.content);
    int textH = textRect.height();
    int totalH = 12 + 22 + 8 + textH + 12;
    totalH = qMax(totalH, 56);
    item->setSizeHint(QSize(0, totalH));

    connect(checkBox, &QCheckBox::toggled, this, [=](bool checked) {
        Database db;
        if (db.open()) {
            if (db.updateTaskStatus(task.id, checked)) {
                contentLabel->setStyleSheet(checked ?
                    "color: #909399; text-decoration: line-through; font-size: 13px; font-weight: 500; background: transparent;" :
                    "color: #303133; font-size: 13px; font-weight: 500; background: transparent;");
            }
        }
    });

    connect(deleteBtn, &QPushButton::clicked, this, [=]() {
        Database db;
        if (db.open()) {
            if (db.deleteTask(task.id)) {
                loadTasksFromDatabase();
            }
        }
    });
}

void Widget::processAutomations()
{
    Database db;
    if (!db.open()) return;

    int userId = db.getUserId(LoginWindow::currentUsername);
    if (userId == -1) return;

    QList<Database::TaskInfo> tasks = db.getTasks(userId);
    if (tasks.isEmpty()) return;

    QDateTime now = QDateTime::currentDateTime();
    QString currentTime = now.toString("HH:mm");

    // 获取室外温度数值
    int outdoorTemp = -999;
    QString outdoorText = ui->outdoorTempLabel->text(); // "室外温度: 22°C"
    QRegularExpression re("\\d+");
    QRegularExpressionMatch match = re.match(outdoorText);
    if (match.hasMatch()) {
        outdoorTemp = match.captured(0).toInt();
    }

    // 获取室内温度 (模拟：取第一个传感器值，若无则设为 25)
    int indoorTemp = 25; 
    QList<Database::DeviceInfo> devices = db.getAllDevices(userId);
    foreach(const auto &d, devices) {
        if (d.type == "传感器") {
            QString state = db.getDeviceState(userId, d.room, d.name);
            QRegularExpressionMatch inMatch = re.match(state);
            if (inMatch.hasMatch()) {
                indoorTemp = inMatch.captured(0).toInt();
                break;
            }
        }
    }

    bool needReload = false;
    foreach(const auto &task, tasks) {
        // 如果任务已禁用/完成，跳过
        if (task.completed) continue;

        bool triggered = false;
        
        // --- 检查触发条件 ---
        if (task.trigger_type == "时间") {
            if (task.trigger_value == currentTime) {
                triggered = true;
            }
        } else if (task.trigger_type == "室内温度" || task.trigger_type == "室外温度") {
            int currentVal = (task.trigger_type == "室内温度") ? indoorTemp : outdoorTemp;
            
            // 如果无法获取室外温度且需要它，则跳过
            if (currentVal == -999) continue;

            // 解析数值比较 (支持 ">26", "<18", "25" 等)
            if (task.trigger_value.startsWith(">")) {
                triggered = (currentVal > task.trigger_value.mid(1).trimmed().toInt());
            } else if (task.trigger_value.startsWith("<")) {
                triggered = (currentVal < task.trigger_value.mid(1).trimmed().toInt());
            } else {
                triggered = (currentVal == task.trigger_value.toInt());
            }
        }

        // --- 执行动作 ---
        if (triggered) {
            qDebug() << "Automation Triggered:" << task.content;
            
            bool actionSuccess = false;
            if (task.action_type == "应用场景") {
                int sceneId = db.getSceneId(userId, task.action_target);
                if (sceneId != -1) {
                    QList<Database::SceneDeviceInfo> sceneDevices = db.getSceneDevices(sceneId);
                    foreach(const auto &sd, sceneDevices) {
                        DeviceCommander::applyDeviceState(userId, sd.roomName, sd.deviceName, sd.deviceType, sd.state);
                    }
                    actionSuccess = true;
                }
            } else if (task.action_type == "控制设备") {
                QString target = task.action_target;
                int start = target.lastIndexOf("(");
                int end = target.lastIndexOf(")");
                if (start != -1 && end != -1) {
                    QString devName = target.left(start).trimmed();
                    QString roomName = target.mid(start + 1, end - start - 1).trimmed();
                    Database::DeviceInfo devInfo = db.getDeviceInfo(userId, roomName, devName);
                    if (DeviceCommander::applyDeviceState(userId, roomName, devName, devInfo.type, task.action_value)) {
                        actionSuccess = true;
                    }
                }
            }

            if (actionSuccess) {
                // 动作执行后，标记为已完成
                db.updateTaskStatus(task.id, true);
                needReload = true;
                
                // 弹出执行成功提示
                QMessageBox::information(this, "自动化执行", 
                    QString("自动化任务已触发并成功执行：\n%1").arg(task.content));
            }
        }
    }
    
    // 如果有任务状态更新，刷新列表
    if (needReload) {
        loadTasksFromDatabase();
    }
}

void Widget::on_logoutButton_clicked()
{
    // 清空当前登录的用户名并取消自动登录
    Database db;
    if (db.open()) {
        db.setAutoLogin(LoginWindow::currentUsername, false);
    }
    
    // 重新显示登录窗口 (在关闭当前窗口之前创建)
    LoginWindow *loginWindow = new LoginWindow();
    
    // 连接新登录窗口的成功信号
    connect(loginWindow, &LoginWindow::loginSuccess, [=]() {
        Widget *w = new Widget();
        w->show();
    });
    
    loginWindow->show();
    
    // 清空当前登录的用户名并关闭当前窗口
    LoginWindow::currentUsername = "";
    this->close();
}

void Widget::on_userManagementButton_clicked()
{
    // 创建用户管理对话框
    QDialog dialog(this);
    dialog.setWindowTitle("用户管理");
    dialog.resize(400, 300);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    // 添加用户列表
    QListWidget *userListWidget = new QListWidget(&dialog);
    layout->addWidget(userListWidget);
    
    // 从数据库加载所有用户
    Database db;
    if (db.open()) {
        QStringList users = db.getAllUsers();
        foreach (const QString &user, users) {
            QListWidgetItem *item = new QListWidgetItem(user);
            // 标记管理员用户
            if (db.isAdmin(user)) {
                item->setText(user + " (管理员)");
            }
            userListWidget->addItem(item);
        }
    }
    
    // 添加按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *addButton = new QPushButton("添加用户");
    QPushButton *deleteButton = new QPushButton("删除用户");
    QPushButton *cancelButton = new QPushButton("取消");
    
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    
    // 连接按钮信号
    connect(addButton, &QPushButton::clicked, [&]() {
        // 创建添加用户对话框
        QDialog addDialog(&dialog);
        addDialog.setWindowTitle("添加用户");
        
        QVBoxLayout *addLayout = new QVBoxLayout(&addDialog);
        
        QLabel *usernameLabel = new QLabel("用户名:");
        QLineEdit *usernameEdit = new QLineEdit();
        QLabel *passwordLabel = new QLabel("密码:");
        QLineEdit *passwordEdit = new QLineEdit();
        passwordEdit->setEchoMode(QLineEdit::Password);
        QCheckBox *adminCheckBox = new QCheckBox("设为管理员");
        
        addLayout->addWidget(usernameLabel);
        addLayout->addWidget(usernameEdit);
        addLayout->addWidget(passwordLabel);
        addLayout->addWidget(passwordEdit);
        addLayout->addWidget(adminCheckBox);
        
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &addDialog);
        addLayout->addWidget(buttonBox);
        
        connect(buttonBox, &QDialogButtonBox::accepted, &addDialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &addDialog, &QDialog::reject);
        
        if (addDialog.exec() == QDialog::Accepted) {
            QString username = usernameEdit->text();
            QString password = passwordEdit->text();
            bool isAdmin = adminCheckBox->isChecked();
            
            if (!username.isEmpty() && !password.isEmpty()) {
                Database db;
                if (db.open()) {
                    if (db.registerUser(username, password, isAdmin)) {
                        // 刷新用户列表
                        userListWidget->clear();
                        QStringList users = db.getAllUsers();
                        foreach (const QString &user, users) {
                            QListWidgetItem *item = new QListWidgetItem(user);
                            if (db.isAdmin(user)) {
                                item->setText(user + " (管理员)");
                            }
                            userListWidget->addItem(item);
                        }
                        QMessageBox::information(&dialog, "成功", "用户添加成功");
                    } else {
                        QMessageBox::warning(&dialog, "错误", "用户添加失败，可能用户名已存在");
                    }
                } else {
                    QMessageBox::warning(&dialog, "错误", "数据库连接失败");
                }
            } else {
                QMessageBox::warning(&dialog, "错误", "请输入用户名和密码");
            }
        }
    });
    
    connect(deleteButton, &QPushButton::clicked, [&]() {
        QListWidgetItem *selectedItem = userListWidget->currentItem();
        if (selectedItem) {
            QString username = selectedItem->text();
            // 移除管理员标记
            if (username.contains(" (管理员)")) {
                username = username.replace(" (管理员)", "");
            }
            
            // 防止删除管理员用户
            if (username == "admin") {
                QMessageBox::warning(&dialog, "错误", "不能删除管理员用户");
                return;
            }
            
            if (QMessageBox::question(&dialog, "确认删除", "确定要删除用户 " + username + " 吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                Database db;
                if (db.open()) {
                    if (db.deleteUser(username)) {
                        // 刷新用户列表
                        userListWidget->clear();
                        QStringList users = db.getAllUsers();
                        foreach (const QString &user, users) {
                            QListWidgetItem *item = new QListWidgetItem(user);
                            if (db.isAdmin(user)) {
                                item->setText(user + " (管理员)");
                            }
                            userListWidget->addItem(item);
                        }
                        QMessageBox::information(&dialog, "成功", "用户删除成功");
                    } else {
                        QMessageBox::warning(&dialog, "错误", "用户删除失败");
                    }
                } else {
                    QMessageBox::warning(&dialog, "错误", "数据库连接失败");
                }
            }
        } else {
            QMessageBox::warning(&dialog, "错误", "请选择要删除的用户");
        }
    });
    
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    dialog.exec();
}

void Widget::updateTime()
{
    QDateTime currentTime = QDateTime::currentDateTime();
    QString timeString = currentTime.toString("时间: HH:mm:ss");
    ui->timeLabel->setText(timeString);
}

void Widget::pollSensorData()
{
    Database db;
    if (!db.open()) return;

    int userId = db.getUserId(LoginWindow::currentUsername);
    if (userId == -1) return;

    QList<Database::DeviceInfo> devices = db.getAllDevices(userId);

    double tempSum = 0, humSum = 0;
    int tempCount = 0, humCount = 0;

    for (const auto &d : devices) {
        if (d.type != "传感器") continue;
        if (d.deviceId.isEmpty()) continue;

        double temperature = 0, humidity = 0;
        int light = 0;
        bool motion = false;

        bool ok = DeviceCommander::fetchSensorDataAsync(d.deviceId,
                                                        temperature, humidity,
                                                        light, motion);
        if (!ok) continue;

        tempSum += temperature;
        humSum += humidity;
        tempCount++;
        humCount++;

        QString stateStr = QString("温度: %1°C, 湿度: %2%%, 光照: %3%, 人体: %4")
                               .arg(temperature, 0, 'f', 1)
                               .arg(humidity, 0, 'f', 1)
                               .arg(light)
                               .arg(motion ? "有" : "无");
        db.updateDeviceState(userId, d.room, d.name, stateStr);
    }

    if (tempCount > 0) {
        indoorTemp = QString::number(tempSum / tempCount, 'f', 1);
        indoorHum = QString::number(humSum / humCount, 'f', 1);
    } else {
        indoorTemp = "--";
        indoorHum = "--";
    }

    ui->tempHumidityLabel->setText(QString("室内温湿度: %1°C / %2%").arg(indoorTemp, indoorHum));
}

void Widget::updateStatusBar()
{
    updateTime();
}

void Widget::on_addRoomButton_clicked()
{
    bool ok;
    QString roomName = QInputDialog::getText(this, "添加房间", "请输入房间名称:", QLineEdit::Normal, "", &ok);
    if (ok && !roomName.isEmpty()) {
        // 保存到数据库
        Database db;
        if (db.open()) {
            int userId = db.getUserId(LoginWindow::currentUsername);
            if (userId != -1) {
            if (db.createRoom(userId, roomName)) {
                // 添加到列表
                addRoomItem(roomName);
            } else {
                    QMessageBox::warning(this, "错误", "添加房间失败");
                }
            }
        } else {
            QMessageBox::warning(this, "错误", "数据库连接失败");
        }
    }
}

// ==================== 插件管理 ====================

void Widget::initPluginTab()
{
    QWidget *pluginTab = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(pluginTab);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    // 标题
    QLabel *titleLabel = new QLabel("插件管理");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #303133;");
    layout->addWidget(titleLabel);

    // 插件列表
    m_pluginListWidget = new QListWidget();
    m_pluginListWidget->setStyleSheet(
        "QListWidget { background-color: #ffffff; border: 1px solid #e4e7ed; border-radius: 8px; padding: 4px; }"
        "QListWidget::item { padding: 10px 12px; border-bottom: 1px solid #f2f6fc; border-radius: 6px; margin: 2px 0; }"
    );
    layout->addWidget(m_pluginListWidget);

    // 状态标签
    m_pluginStatusLabel = new QLabel("");
    m_pluginStatusLabel->setStyleSheet("font-size: 12px; color: #909399;");
    layout->addWidget(m_pluginStatusLabel);

    // 按钮区域
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);

    m_pluginReloadBtn = new QPushButton("重新加载插件");
    m_pluginReloadBtn->setStyleSheet(
        "QPushButton { background-color: #409eff; color: white; border: none; border-radius: 8px; "
        "padding: 10px 24px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: #66b1ff; }"
    );
    connect(m_pluginReloadBtn, &QPushButton::clicked, this, &Widget::refreshPluginList);

    m_pluginCompileBtn = new QPushButton("编译插件");
    m_pluginCompileBtn->setStyleSheet(
        "QPushButton { background-color: #67c23a; color: white; border: none; border-radius: 8px; "
        "padding: 10px 24px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: #85ce61; }"
        "QPushButton:disabled { background-color: #c0c4cc; }"
    );
    connect(m_pluginCompileBtn, &QPushButton::clicked, this, &Widget::compilePlugins);

    m_pluginRemoveBtn = new QPushButton("移除插件");
    m_pluginRemoveBtn->setStyleSheet(
        "QPushButton { background-color: #f56c6c; color: white; border: none; border-radius: 8px; "
        "padding: 10px 24px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: #f78989; }"
        "QPushButton:disabled { background-color: #c0c4cc; }"
    );
    connect(m_pluginRemoveBtn, &QPushButton::clicked, this, [this]() {
        int row = m_pluginListWidget->currentRow();
        if (row < 0) {
            m_pluginStatusLabel->setText("请先选择要移除的插件");
            return;
        }
        QListWidgetItem *item = m_pluginListWidget->item(row);
        if (!item) return;
        QString deviceType = item->data(Qt::UserRole).toString();
        if (deviceType.isEmpty()) {
            m_pluginStatusLabel->setText("该插件无法移除");
            return;
        }
        PluginManager *pm = PluginManager::instance();
        DevicePluginInterface *plugin = pm->pluginForType(deviceType);
        if (!plugin) {
            m_pluginStatusLabel->setText("未找到该插件");
            return;
        }
        QString name = plugin->displayName();
        QMessageBox::StandardButton reply = QMessageBox::question(this, "移除插件",
            QString("确定要移除插件「%1」吗？\n插件 .so 文件将被删除，需要重新编译才能恢复。").arg(name),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        if (pm->removePlugin(plugin)) {
            m_pluginStatusLabel->setText(QString("已移除插件「%1」").arg(name));
            refreshPluginList();
        } else {
            m_pluginStatusLabel->setText("移除失败：无法删除插件文件");
        }
    });

    btnLayout->addWidget(m_pluginReloadBtn);
    btnLayout->addWidget(m_pluginCompileBtn);
    btnLayout->addWidget(m_pluginRemoveBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    ui->mainTabWidget->addTab(pluginTab, "插件管理");
    refreshPluginList();
}

void Widget::refreshPluginList()
{
    m_pluginListWidget->clear();

    PluginManager *pm = PluginManager::instance();
    pm->reloadPlugins();
    QList<DevicePluginInterface*> plugins = pm->plugins();

    if (plugins.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem("暂无已加载的插件");
        item->setForeground(QBrush(QColor("#c0c4cc")));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        m_pluginListWidget->addItem(item);
        m_pluginStatusLabel->setText("提示：将插件 .so 文件放入 plugins/ 目录后点击「重新加载插件」，或点击「编译插件」从源码编译");
    } else {
        for (DevicePluginInterface *plugin : plugins) {
            QWidget *widget = new QWidget();
            QHBoxLayout *hLayout = new QHBoxLayout(widget);
            hLayout->setContentsMargins(8, 4, 8, 4);

            QLabel *iconLabel = new QLabel(plugin->iconText());
            iconLabel->setStyleSheet("font-size: 20px; background: transparent; border: none;");
            iconLabel->setFixedWidth(32);

            QVBoxLayout *infoLayout = new QVBoxLayout();
            infoLayout->setSpacing(2);
            QLabel *nameLabel = new QLabel(plugin->displayName());
            nameLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #303133; background: transparent; border: none;");
            QLabel *descLabel = new QLabel(plugin->description() + "  |  类型: " + plugin->deviceType());
            descLabel->setStyleSheet("font-size: 12px; color: #909399; background: transparent; border: none;");
            infoLayout->addWidget(nameLabel);
            infoLayout->addWidget(descLabel);

            hLayout->addWidget(iconLabel);
            hLayout->addLayout(infoLayout, 1);

            QListWidgetItem *item = new QListWidgetItem();
            item->setData(Qt::UserRole, plugin->deviceType());
            m_pluginListWidget->addItem(item);
            m_pluginListWidget->setItemWidget(item, widget);
            item->setSizeHint(QSize(0, 56));
        }
        m_pluginStatusLabel->setText(QString("已加载 %1 个插件").arg(plugins.size()));
    }
}

void Widget::compilePlugins()
{
    // 查找插件源码目录（必须包含 .pro 文件才算源码目录）
    QString appDir = QCoreApplication::applicationDirPath();
    QString srcPluginDir;
    QStringList searchPaths = {
        appDir + "/../../plugins",
        appDir + "/../plugins",
        QDir::currentPath() + "/plugins",
        QDir::currentPath() + "/../plugins"
    };
    auto hasProFiles = [](const QString &dir) {
        QDir d(dir);
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFile::exists(d.filePath(sub + "/" + sub + ".pro")))
                return true;
        }
        return false;
    };
    for (const QString &path : searchPaths) {
        if (QDir(path).exists() && hasProFiles(path)) {
            srcPluginDir = QDir::cleanPath(path);
            break;
        }
    }

    if (srcPluginDir.isEmpty()) {
        m_pluginStatusLabel->setText("未找到插件源码目录 (plugins/)");
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
        m_pluginStatusLabel->setText("未找到插件项目文件 (.pro)");
        return;
    }

    // 查找 qmake
    QString qmakePath;
    QStringList qmakeSearch = {
        QDir::homePath() + "/Qt/5.15.2/gcc_64/bin/qmake",
        appDir + "/../../../Qt/5.15.2/gcc_64/bin/qmake",
        "/usr/bin/qmake",
        "/usr/local/bin/qmake"
    };
    for (const QString &p : qmakeSearch) {
        if (QFile::exists(p)) { qmakePath = p; break; }
    }
    if (qmakePath.isEmpty()) {
        QProcess whichProc;
        whichProc.start("which", {"qmake"});
        whichProc.waitForFinished(3000);
        if (whichProc.exitCode() == 0)
            qmakePath = QString(whichProc.readAllStandardOutput().trimmed());
    }
    if (qmakePath.isEmpty()) {
        m_pluginStatusLabel->setText("未找到 qmake，请确保已安装 Qt 开发环境");
        return;
    }

    m_compileIndex = 0;
    m_qmakePath = qmakePath;
    m_pluginCompileBtn->setEnabled(false);
    m_pluginCompileBtn->setText("编译中...");
    m_pluginStatusLabel->setText(QString("正在编译插件 (0/%1)...").arg(m_compileQueue.size()));

    // 从源码目录推导项目根目录和构建目录
    // srcPluginDir = "<projectRoot>/plugins", 所以 projectRoot = srcPluginDir/..
    QString projectRoot = QDir::cleanPath(srcPluginDir + "/..");
    m_buildBaseDir = projectRoot + "/build/plugins";

    // 开始编译第一个
    compileNextInQueue();
}

void Widget::compileNextInQueue()
{
    if (m_compileIndex >= m_compileQueue.size()) {
        m_pluginCompileBtn->setEnabled(true);
        m_pluginCompileBtn->setText("编译插件");
        refreshPluginList();
        return;
    }

    QString proFile = m_compileQueue[m_compileIndex];
    QString pluginName = QFileInfo(proFile).baseName();
    m_pluginStatusLabel->setText(QString("正在编译 %1 (%2/%3)...")
        .arg(pluginName).arg(m_compileIndex + 1).arg(m_compileQueue.size()));

    m_currentBuildDir = m_buildBaseDir + "/" + pluginName;
    QDir().mkpath(m_currentBuildDir);

    QProcess *qmakeProc = new QProcess(this);
    qmakeProc->setWorkingDirectory(m_currentBuildDir);
    connect(qmakeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Widget::onPluginQmakeFinished);
    qmakeProc->start(m_qmakePath, {proFile});
}

void Widget::onPluginQmakeFinished(int exitCode, QProcess::ExitStatus)
{
    QProcess *proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    if (exitCode != 0) {
        m_pluginStatusLabel->setText("qmake 失败: " + QString(proc->readAllStandardError().trimmed()).left(100));
        m_pluginCompileBtn->setEnabled(true);
        m_pluginCompileBtn->setText("编译插件");
        proc->deleteLater();
        return;
    }
    proc->deleteLater();

    QProcess *makeProc = new QProcess(this);
    makeProc->setWorkingDirectory(m_currentBuildDir);
    connect(makeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Widget::onPluginMakeFinished);
    makeProc->start("make", {"-j" + QString::number(QThread::idealThreadCount())});
}

void Widget::onPluginMakeFinished(int exitCode, QProcess::ExitStatus)
{
    QProcess *proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;

    if (exitCode != 0) {
        QString err = QString(proc->readAllStandardError()).trimmed();
        m_pluginStatusLabel->setText("编译失败: " + err.left(100));
        m_pluginCompileBtn->setEnabled(true);
        m_pluginCompileBtn->setText("编译插件");
        proc->deleteLater();
        return;
    }
    proc->deleteLater();

    m_compileIndex++;
    compileNextInQueue();
}


