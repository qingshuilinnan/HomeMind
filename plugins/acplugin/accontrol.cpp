#include "accontrol.h"
#include "ui_accontrol.h"
#include "database.h"
#include "deviceplugininterface.h"
#include <QMessageBox>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QDialog>
#include <QJsonObject>
#include <QTimer>

static const QString POWER_ON_STYLE =
    "border-radius: 40px; background-color: #4facfe; color: white; font-size: 16px; font-weight: bold; border: 3px solid rgba(255,255,255,0.6);";
static const QString POWER_OFF_STYLE =
    "border-radius: 40px; background-color: #c0c4cc; color: white; font-size: 16px; font-weight: bold; border: 3px solid rgba(255,255,255,0.6);";

static const QString GRADIENT_ON_STYLE =
    "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #4facfe, stop:1 #00f2fe);"
    "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;";
static const QString GRADIENT_OFF_STYLE =
    "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #909399, stop:1 #b0b3b8);"
    "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;";

ACControl::ACControl(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ACControl)
    , isPowerOn(false)
    , currentTemperature(26)
{
    ui->setupUi(this);
    updateTemperatureDisplay();
}

ACControl::~ACControl()
{
    delete ui;
}

void ACControl::setHostServices(HostServices *services)
{
    m_services = services;
}

void ACControl::setDeviceInfo(const QString &roomName, const QString &deviceName)
{
    m_roomName = roomName;
    m_deviceName = deviceName;
    setWindowTitle(deviceName + " - 空调控制");
    ui->deviceNameLabel->setText(deviceName);

    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        if (userId != -1) {
            Database::DeviceInfo info = db.getDeviceInfo(userId, roomName, deviceName);
            m_deviceId = info.deviceId;
        }
    }

    loadState();
}

void ACControl::loadState()
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        QString state = db.getDeviceState(userId, m_roomName, m_deviceName);
        if (!state.isEmpty()) {
            if (state.startsWith("开启")) {
                isPowerOn = true;
                QStringList parts = state.split(", ");
                if (parts.size() > 1) {
                    QString tempStr = parts[1].replace("°C", "");
                    currentTemperature = tempStr.toInt();
                }
            } else {
                isPowerOn = false;
            }
            updateUI();
        }
    }
}

void ACControl::saveState()
{
    if (m_deviceName.isEmpty()) return;

    QString state;
    if (isPowerOn) {
        state = QString("开启, %1°C").arg(currentTemperature);
    } else {
        state = "关闭";
    }

    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        db.updateDeviceState(userId, m_roomName, m_deviceName, state);
    }
}

void ACControl::updateUI()
{
    ui->powerButton->setText(isPowerOn ? "关闭" : "开启");
    ui->powerButton->setStyleSheet(isPowerOn ? POWER_ON_STYLE : POWER_OFF_STYLE);
    ui->topCard->setStyleSheet(isPowerOn ? GRADIENT_ON_STYLE : GRADIENT_OFF_STYLE);
    ui->statusLabel->setText(isPowerOn ? "运行中 · 制冷模式" : "已关机");
    ui->temperatureUpButton->setEnabled(isPowerOn);
    ui->temperatureDownButton->setEnabled(isPowerOn);
    updateTemperatureDisplay();
}

void ACControl::updateTemperatureDisplay()
{
    ui->temperatureLabel->setText(QString::number(currentTemperature) + "°");
}

void ACControl::on_powerButton_clicked()
{
    isPowerOn = !isPowerOn;
    updateUI();
    saveState();
    sendCommand({{"type", "power"}, {"value", isPowerOn ? "true" : "false"}});
}

void ACControl::on_temperatureUpButton_clicked()
{
    if (isPowerOn && currentTemperature < 30) {
        currentTemperature++;
        updateTemperatureDisplay();
        saveState();
        sendCommand({{"type", "temperature"}, {"value", currentTemperature}});
    }
}

void ACControl::on_temperatureDownButton_clicked()
{
    if (isPowerOn && currentTemperature > 16) {
        currentTemperature--;
        updateTemperatureDisplay();
        saveState();
        sendCommand({{"type", "temperature"}, {"value", currentTemperature}});
    }
}

void ACControl::on_fanSpeedComboBox_currentIndexChanged(int index)
{
    if (isPowerOn) {
        sendCommand({{"type", "fanSpeed"}, {"value", index}});
    }
}

void ACControl::on_verticalSwingCheckBox_stateChanged(int arg1)
{
    Q_UNUSED(arg1);
    if (isPowerOn) {
        sendCommand({{"type", "swing"}, {"value", arg1 == Qt::Checked ? "true" : "false"}});
    }
}

void ACControl::on_horizontalSwingCheckBox_stateChanged(int arg1)
{
    Q_UNUSED(arg1);
}

void ACControl::on_timerButton_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("设置定时");
    dialog.setStyleSheet(
        "QDialog { background-color: #f5f7fa; }"
        "QTimeEdit { background-color: white; border: 1px solid #dcdfe6; border-radius: 6px; padding: 8px; font-size: 14px; }"
        "QDialogButtonBox QPushButton { background-color: #4facfe; color: white; border: none; border-radius: 6px; padding: 8px 20px; font-weight: bold; }"
        "QDialogButtonBox QPushButton:hover { background-color: #66b1ff; }"
    );

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel *timerLabel = new QLabel("选择关机时间:", &dialog);
    timerLabel->setStyleSheet("color: #606266; font-size: 14px; font-weight: 500;");
    layout->addWidget(timerLabel);

    QTimeEdit *timeEdit = new QTimeEdit(&dialog);
    timeEdit->setDisplayFormat("HH:mm");
    layout->addWidget(timeEdit);

    layout->addSpacing(5);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && isPowerOn) {
        QTime selectedTime = timeEdit->time();
        int minutes = QTime::currentTime().secsTo(selectedTime) / 60;
        if (minutes < 0) minutes += 24 * 60;
        sendCommand({{"type", "timer"}, {"value", minutes}});
    }
}

void ACControl::sendCommand(const QJsonObject &cmd)
{
    if (m_deviceId.isEmpty() || !m_services || !m_services->sendCommand) return;
    m_services->sendCommand(m_deviceId, cmd);
}
