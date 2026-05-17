#include "sensorplugin.h"
#include "sensorcontrol.h"
#include "database.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>
#include <QTimer>
#include "devicediscovery.h"

SensorPlugin::SensorPlugin(QObject *parent)
    : QObject(parent)
{
}

QString SensorPlugin::deviceType() const { return "传感器"; }
QString SensorPlugin::displayName() const { return "传感器"; }
QString SensorPlugin::description() const { return "温湿度、光照、人体感应传感器"; }
QString SensorPlugin::iconText() const { return "\xF0\x9F\x8C\xA1"; }
QString SensorPlugin::mqttDeviceType() const { return "sensor_device"; }
bool SensorPlugin::supportsScan() const { return true; }

void SensorPlugin::initialize(HostServices *services)
{
    m_services = services;
}

QWidget *SensorPlugin::createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent)
{
    SensorControl *control = new SensorControl(parent);
    if (m_services) {
        control->setHostServices(m_services);
    }
    control->setDeviceInfo(roomName, deviceName);
    return control;
}

QList<QJsonObject> SensorPlugin::buildCommands(const QString &state) const
{
    Q_UNUSED(state);
    return QList<QJsonObject>(); // sensors are read-only
}

QWidget *SensorPlugin::createSceneStateEditor(QWidget *parent)
{
    // Sensors don't participate in scenes
    return nullptr;
}

QString SensorPlugin::sceneEditorResult(QWidget *editor) const
{
    Q_UNUSED(editor);
    return "";
}

QString SensorPlugin::stateDisplayText(const QString &state) const
{
    if (state.isEmpty() || state == "未设置") return "\xE2\x9A\xAA 未知";
    return "\xE2\x9A\xAA " + state;
}

bool SensorPlugin::showAddDeviceDialog(QWidget *parent, const QString &roomName, const QString &username)
{
    if (!m_services || !m_services->discovery) return false;

    DeviceDiscovery *discovery = m_services->discovery;
    if (!discovery->isBrokerConnected()) {
        QMessageBox::warning(parent, "连接错误", "MQTT Broker 未连接，请稍候再试");
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("扫描添加传感器设备");
    dialog.resize(420, 480);
    dialog.setStyleSheet(
        "QDialog { background-color: #f5f7fa; }"
        "QLabel { color: #303133; }"
        "QListWidget { background-color: #ffffff; border: 1px solid #e4e7ed; border-radius: 8px; outline: none; padding: 4px; }"
        "QListWidget::item { padding: 8px 12px; border-bottom: 1px solid #f2f6fc; border-radius: 6px; margin: 2px 0; }"
        "QListWidget::item:selected { background-color: #ecf5ff; color: #409eff; }"
        "QListWidget::item:hover { background-color: #f5f7fa; }"
        "QPushButton { border-radius: 6px; padding: 8px 16px; font-size: 13px; font-weight: bold; }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel("搜索局域网中的传感器设备", &dialog);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #303133;");
    mainLayout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(QString::fromUtf8("点击「开始扫描」后，将通过 MQTT 自动发现同一网络下的 ESP8266 温湿度传感器"), &dialog);
    hintLabel->setStyleSheet("color: #909399; font-size: 12px;");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(hintLabel);

    QListWidget *deviceList = new QListWidget(&dialog);
    mainLayout->addWidget(deviceList);

    QLabel *statusLabel = new QLabel("等待扫描...", &dialog);
    statusLabel->setStyleSheet("color: #909399; font-size: 12px;");
    mainLayout->addWidget(statusLabel);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *scanButton = new QPushButton("开始扫描", &dialog);
    scanButton->setStyleSheet(
        "QPushButton { background-color: #67c23a; color: white; }"
        "QPushButton:hover { background-color: #85ce61; }"
        "QPushButton:disabled { background-color: #c0c4cc; }"
    );

    QPushButton *manualButton = new QPushButton("手动添加", &dialog);
    manualButton->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #606266; border: 1px solid #dcdfe6; }"
        "QPushButton:hover { color: #409eff; border-color: #c6e2ff; background-color: #ecf5ff; }"
    );

    buttonLayout->addWidget(scanButton);
    buttonLayout->addWidget(manualButton);
    mainLayout->addLayout(buttonLayout);

    QDialogButtonBox *confirmBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *okButton = confirmBox->button(QDialogButtonBox::Ok);
    okButton->setText("添加");
    okButton->setEnabled(false);
    okButton->setStyleSheet(
        "QPushButton { background-color: #67c23a; color: white; }"
        "QPushButton:hover { background-color: #85ce61; }"
        "QPushButton:disabled { background-color: #c0c4cc; }"
    );
    QPushButton *cancelButton = confirmBox->button(QDialogButtonBox::Cancel);
    cancelButton->setText("取消");
    mainLayout->addWidget(confirmBox);

    QSet<QString> addedDeviceIds;
    Database db;
    int userId = -1;
    if (db.open()) {
        userId = db.getUserId(username);
        QList<Database::DeviceInfo> existing = db.getAllDevices(userId);
        for (const auto &d : existing) {
            if (!d.deviceId.isEmpty()) {
                addedDeviceIds.insert(d.deviceId);
            }
        }
    }

    auto refreshSensorList = [deviceList, addedDeviceIds, discovery]() {
        deviceList->clear();
        QMap<QString, DeviceDiscovery::DiscoveredDevice> devices = discovery->discoveredDevices();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            const DeviceDiscovery::DiscoveredDevice &dev = it.value();
            if (dev.deviceType != "sensor_device") continue;
            QListWidgetItem *item = new QListWidgetItem();
            QString displayText;
            if (addedDeviceIds.contains(dev.deviceId)) {
                displayText = QString::fromUtf8("传感器 - %1 (%2) 已添加").arg(dev.deviceName, dev.ip);
                item->setForeground(QBrush(QColor("#c0c4cc")));
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            } else {
                displayText = QString("传感器 - %1 (%2)").arg(dev.deviceName, dev.ip);
            }
            item->setText(displayText);
            item->setData(Qt::UserRole, dev.deviceId);
            deviceList->addItem(item);
        }
    };

    connect(discovery, &DeviceDiscovery::deviceFound, &dialog, [refreshSensorList, statusLabel, discovery]() {
        refreshSensorList();
        int count = 0;
        auto devices = discovery->discoveredDevices();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if (it.value().deviceType == "sensor_device") count++;
        }
        statusLabel->setText(QString("已发现 %1 台传感器").arg(count));
    });

    connect(scanButton, &QPushButton::clicked, &dialog, [scanButton, statusLabel, discovery, refreshSensorList, &dialog]() {
        scanButton->setEnabled(false);
        scanButton->setText("扫描中...");
        statusLabel->setText("正在扫描局域网传感器设备，请稍候...");
        discovery->requestDeviceScan();
        refreshSensorList();

        QTimer::singleShot(5000, &dialog, [scanButton, statusLabel, refreshSensorList, discovery]() {
            scanButton->setEnabled(true);
            scanButton->setText("重新扫描");
            int count = 0;
            auto devices = discovery->discoveredDevices();
            for (auto it = devices.begin(); it != devices.end(); ++it) {
                if (it.value().deviceType == "sensor_device") count++;
            }
            if (count == 0) {
                statusLabel->setText("未发现传感器设备，请确认设备已开机并连接到同一网络");
            } else {
                statusLabel->setText(QString("扫描完成，共发现 %1 台传感器").arg(count));
            }
            refreshSensorList();
        });
    });

    connect(deviceList, &QListWidget::itemClicked, &dialog, [okButton](QListWidgetItem *item) {
        if (item->text().contains(QString::fromUtf8("已添加"))) {
            okButton->setEnabled(false);
        } else {
            okButton->setEnabled(true);
        }
    });

    connect(manualButton, &QPushButton::clicked, &dialog, [this, &dialog, userId, &roomName, username]() {
        QDialog manualDialog(&dialog);
        manualDialog.setWindowTitle("手动添加传感器");
        manualDialog.setStyleSheet(
            "QDialog { background-color: #f5f7fa; }"
            "QLabel { color: #303133; font-size: 13px; }"
            "QLineEdit { background-color: white; border: 1px solid #dcdfe6; border-radius: 6px; padding: 8px; font-size: 14px; }"
            "QLineEdit:focus { border: 1px solid #409eff; }"
            "QPushButton { border-radius: 6px; padding: 8px 16px; font-size: 13px; font-weight: bold; }"
        );

        QVBoxLayout *mLayout = new QVBoxLayout(&manualDialog);
        mLayout->setContentsMargins(20, 20, 20, 20);
        mLayout->setSpacing(12);

        mLayout->addWidget(new QLabel("设备名称:", &manualDialog));
        QLineEdit *nameEdit = new QLineEdit(&manualDialog);
        nameEdit->setPlaceholderText("例如：卧室温湿度传感器");
        mLayout->addWidget(nameEdit);

        mLayout->addWidget(new QLabel("设备 ID:", &manualDialog));
        QLineEdit *idEdit = new QLineEdit(&manualDialog);
        idEdit->setPlaceholderText("例如：SENSOR_BEDROOM_001");
        mLayout->addWidget(idEdit);

        mLayout->addWidget(new QLabel("设备 IP 地址:", &manualDialog));
        QLineEdit *ipEdit = new QLineEdit(&manualDialog);
        ipEdit->setPlaceholderText("例如：192.168.1.110");
        mLayout->addWidget(ipEdit);

        mLayout->addWidget(new QLabel("TCP 端口:", &manualDialog));
        QLineEdit *portEdit = new QLineEdit(&manualDialog);
        portEdit->setText("8080");
        mLayout->addWidget(portEdit);

        QDialogButtonBox *mBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &manualDialog);
        mLayout->addWidget(mBox);
        connect(mBox, &QDialogButtonBox::accepted, &manualDialog, &QDialog::accept);
        connect(mBox, &QDialogButtonBox::rejected, &manualDialog, &QDialog::reject);

        if (manualDialog.exec() == QDialog::Accepted) {
            QString devName = nameEdit->text().trimmed();
            QString devId = idEdit->text().trimmed();
            QString devIp = ipEdit->text().trimmed();
            int devPort = portEdit->text().toInt();

            if (devName.isEmpty() || devId.isEmpty()) {
                QMessageBox::warning(&dialog, "错误", "设备名称和 ID 不能为空");
                return;
            }

            Database db;
            if (userId != -1 && db.open()) {
                if (db.isDeviceNameExists(userId, roomName, devName, "传感器")) {
                    QMessageBox::warning(&dialog, "错误", "设备名称已存在");
                    return;
                }
                if (db.createDevice(userId, roomName, devName, "传感器", "", devIp, devPort, devId)) {
                    dialog.accept();
                }
            }
        }
    });

    connect(confirmBox, &QDialogButtonBox::accepted, &dialog, [&dialog, userId, deviceList, discovery, &roomName]() {
        QListWidgetItem *selected = deviceList->currentItem();
        if (!selected || selected->text().contains("已添加")) {
            return;
        }

        QString deviceId = selected->data(Qt::UserRole).toString();
        DeviceDiscovery::DiscoveredDevice dev = discovery->getDevice(deviceId);
        QString defaultName = "传感器-" + dev.ip.section('.', -1);

        bool ok;
        QString deviceName = QInputDialog::getText(&dialog, "设备命名",
            "请输入设备名称:", QLineEdit::Normal, defaultName, &ok);
        if (!ok || deviceName.trimmed().isEmpty()) {
            return;
        }
        deviceName = deviceName.trimmed();

        Database db;
        if (db.open() && userId != -1) {
            if (db.isDeviceNameExists(userId, roomName, deviceName, "传感器")) {
                QMessageBox::warning(&dialog, "错误", "设备名称已存在");
                return;
            }
            if (db.createDevice(userId, roomName, deviceName, "传感器", "", dev.ip, dev.tcpPort, deviceId)) {
                dialog.accept();
            }
        }
    });

    return dialog.exec() == QDialog::Accepted;
}
