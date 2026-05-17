#include "acplugin.h"
#include "accontrol.h"
#include "database.h"
#include "devicediscovery.h"
#include "devicecommander.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>
#include <QTimer>

ACPlugin::ACPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ACPlugin::deviceType() const { return "空调"; }
QString ACPlugin::displayName() const { return "空调控制"; }
QString ACPlugin::description() const { return "空调设备的开关、温度、风速控制"; }
QString ACPlugin::iconText() const { return "\xE2\x98\x81\xEF\xB8\x8F"; }
QString ACPlugin::mqttDeviceType() const { return "ac_device"; }

void ACPlugin::initialize(HostServices *services)
{
    m_services = services;
}

QWidget *ACPlugin::createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent)
{
    ACControl *control = new ACControl(parent);
    if (m_services) {
        control->setHostServices(m_services);
    }
    control->setDeviceInfo(roomName, deviceName);
    return control;
}

QList<QJsonObject> ACPlugin::buildCommands(const QString &state) const
{
    QList<QJsonObject> commands;
    if (state.isEmpty()) return commands;

    bool isOn = state.startsWith("开启");
    QJsonObject powerCmd;
    powerCmd["type"] = "power";
    powerCmd["value"] = isOn ? "true" : "false";
    commands.append(powerCmd);

    if (isOn && state.contains("°C")) {
        QString tempStr = state;
        tempStr.remove("开启, ");
        tempStr.remove("°C");
        int temp = tempStr.toInt();
        if (temp >= 16 && temp <= 30) {
            QJsonObject tempCmd;
            tempCmd["type"] = "temperature";
            tempCmd["value"] = temp;
            commands.append(tempCmd);
        }
    }

    return commands;
}

QWidget *ACPlugin::createSceneStateEditor(QWidget *parent)
{
    QWidget *editor = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editor);

    QComboBox *powerCb = new QComboBox(editor);
    powerCb->addItems({"开启", "关闭"});
    powerCb->setObjectName("powerCombo");

    QSpinBox *tempSb = new QSpinBox(editor);
    tempSb->setRange(16, 30);
    tempSb->setValue(26);
    tempSb->setObjectName("tempSpin");

    layout->addWidget(new QLabel("开关:"));
    layout->addWidget(powerCb);
    layout->addWidget(new QLabel("温度 (16-30°C):"));
    layout->addWidget(tempSb);

    return editor;
}

QString ACPlugin::sceneEditorResult(QWidget *editor) const
{
    QComboBox *powerCb = editor->findChild<QComboBox*>("powerCombo");
    QSpinBox *tempSb = editor->findChild<QSpinBox*>("tempSpin");
    if (!powerCb || !tempSb) return "";

    int val = tempSb->value();
    if (val < 16 || val > 30) return "";
    return QString("%1, %2°C").arg(powerCb->currentText()).arg(val);
}

QString ACPlugin::stateDisplayText(const QString &state) const
{
    if (state.isEmpty() || state == "未设置") return "\xE2\x9A\xAA 未知";
    if (state.startsWith("开启")) return "\xE2\x9A\xAA 运行中";
    if (state == "关闭") return "\xE2\x98\xAA 已关机";
    return "\xE2\x9A\xAA " + state;
}

bool ACPlugin::supportsScan() const
{
    return true;
}

bool ACPlugin::showAddDeviceDialog(QWidget *parent, const QString &roomName, const QString &username)
{
    if (!m_services || !m_services->discovery) return false;

    DeviceDiscovery *discovery = m_services->discovery;
    if (!discovery->isBrokerConnected()) {
        QMessageBox::warning(parent, "连接错误", "MQTT Broker 未连接，请稍候再试");
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("扫描添加空调设备");
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

    QLabel *titleLabel = new QLabel("搜索局域网中的空调设备", &dialog);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #303133;");
    mainLayout->addWidget(titleLabel);

    QLabel *hintLabel = new QLabel(QString::fromUtf8("点击「开始扫描」后，将通过 MQTT 自动发现同一网络下的 ESP32 空调控制器"), &dialog);
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
        "QPushButton { background-color: #409eff; color: white; }"
        "QPushButton:hover { background-color: #66b1ff; }"
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
        "QPushButton { background-color: #409eff; color: white; }"
        "QPushButton:hover { background-color: #66b1ff; }"
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

    auto refreshAcList = [deviceList, addedDeviceIds, discovery]() {
        deviceList->clear();
        QMap<QString, DeviceDiscovery::DiscoveredDevice> devices = discovery->discoveredDevices();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            const DeviceDiscovery::DiscoveredDevice &dev = it.value();
            if (dev.deviceType != "ac_device") continue;
            QListWidgetItem *item = new QListWidgetItem();
            QString displayText;
            if (addedDeviceIds.contains(dev.deviceId)) {
                displayText = QString::fromUtf8("空调 - %1 (%2) 已添加").arg(dev.deviceName, dev.ip);
                item->setForeground(QBrush(QColor("#c0c4cc")));
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            } else {
                displayText = QString("空调 - %1 (%2)").arg(dev.deviceName, dev.ip);
            }
            item->setText(displayText);
            item->setData(Qt::UserRole, dev.deviceId);
            deviceList->addItem(item);
        }
    };

    connect(discovery, &DeviceDiscovery::deviceFound, &dialog, [refreshAcList, statusLabel, discovery]() {
        refreshAcList();
        int count = 0;
        auto devices = discovery->discoveredDevices();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if (it.value().deviceType == "ac_device") count++;
        }
        statusLabel->setText(QString("已发现 %1 台空调设备").arg(count));
    });

    connect(scanButton, &QPushButton::clicked, &dialog, [scanButton, statusLabel, discovery, refreshAcList, &dialog]() {
        scanButton->setEnabled(false);
        scanButton->setText("扫描中...");
        statusLabel->setText("正在扫描局域网设备，请稍候...");
        discovery->requestDeviceScan();
        refreshAcList();

        QTimer::singleShot(5000, &dialog, [scanButton, statusLabel, refreshAcList, discovery]() {
            scanButton->setEnabled(true);
            scanButton->setText("重新扫描");
            int count = 0;
            auto devices = discovery->discoveredDevices();
            for (auto it = devices.begin(); it != devices.end(); ++it) {
                if (it.value().deviceType == "ac_device") count++;
            }
            if (count == 0) {
                statusLabel->setText("未发现空调设备，请确认设备已开机并连接到同一网络");
            } else {
                statusLabel->setText(QString("扫描完成，共发现 %1 台空调设备").arg(count));
            }
            refreshAcList();
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
        manualDialog.setWindowTitle("手动添加空调");
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
        nameEdit->setPlaceholderText("例如：客厅空调");
        mLayout->addWidget(nameEdit);

        mLayout->addWidget(new QLabel("设备 ID:", &manualDialog));
        QLineEdit *idEdit = new QLineEdit(&manualDialog);
        idEdit->setPlaceholderText("例如：AC_LIVING_001");
        mLayout->addWidget(idEdit);

        mLayout->addWidget(new QLabel("设备 IP 地址:", &manualDialog));
        QLineEdit *ipEdit = new QLineEdit(&manualDialog);
        ipEdit->setPlaceholderText("例如：192.168.1.105");
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
                if (db.isDeviceNameExists(userId, roomName, devName, "空调")) {
                    QMessageBox::warning(&dialog, "错误", "设备名称已存在");
                    return;
                }
                if (db.createDevice(userId, roomName, devName, "空调", "", devIp, devPort, devId)) {
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
        QString defaultName = "空调-" + dev.ip.section('.', -1);

        bool ok;
        QString deviceName = QInputDialog::getText(&dialog, "设备命名",
            "请输入设备名称:", QLineEdit::Normal, defaultName, &ok);
        if (!ok || deviceName.trimmed().isEmpty()) {
            return;
        }
        deviceName = deviceName.trimmed();

        Database db;
        if (db.open() && userId != -1) {
            if (db.isDeviceNameExists(userId, roomName, deviceName, "空调")) {
                QMessageBox::warning(&dialog, "错误", "设备名称已存在");
                return;
            }
            if (db.createDevice(userId, roomName, deviceName, "空调", "", dev.ip, dev.tcpPort, deviceId)) {
                dialog.accept();
            }
        }
    });

    return dialog.exec() == QDialog::Accepted;
}

bool ACPlugin::showManualAddDialog(QWidget *parent, const QString &roomName, const QString &username)
{
    Q_UNUSED(parent);
    Q_UNUSED(roomName);
    Q_UNUSED(username);
    return false;
}
