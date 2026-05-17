#include "sensorcontrol.h"
#include "database.h"
#include "deviceplugininterface.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QDateTime>
#include <QDebug>

SensorControl::SensorControl(QWidget *parent)
    : QWidget(parent)
    , m_refreshTimer(new QTimer(this))
{
    buildUI();
    connect(m_refreshTimer, &QTimer::timeout, this, &SensorControl::fetchFromDevice);
}

SensorControl::~SensorControl()
{
}

void SensorControl::setHostServices(HostServices *services)
{
    m_services = services;
}

void SensorControl::buildUI()
{
    setFixedSize(380, 520);
    setStyleSheet("background-color: #f5f7fa;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_topCard = new QWidget(this);
    m_topCard->setFixedHeight(180);
    m_topCard->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #43e97b, stop:1 #38f9d7);"
        "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;"
    );
    QVBoxLayout *topLayout = new QVBoxLayout(m_topCard);
    topLayout->setContentsMargins(24, 20, 24, 16);
    topLayout->setSpacing(8);

    m_deviceNameLabel = new QLabel(m_topCard);
    m_deviceNameLabel->setStyleSheet("color: white; font-size: 20px; font-weight: bold; background: transparent;");
    topLayout->addWidget(m_deviceNameLabel);

    m_statusLabel = new QLabel("数据更新中...", m_topCard);
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.85); font-size: 13px; background: transparent;");
    topLayout->addWidget(m_statusLabel);

    topLayout->addStretch();

    QHBoxLayout *bigTempLayout = new QHBoxLayout();
    bigTempLayout->setSpacing(4);

    QLabel *tempIcon = new QLabel("🌡", m_topCard);
    tempIcon->setStyleSheet("font-size: 28px; background: transparent;");
    bigTempLayout->addWidget(tempIcon);

    m_tempValueLabel = new QLabel("--", m_topCard);
    m_tempValueLabel->setStyleSheet("color: white; font-size: 48px; font-weight: bold; background: transparent;");
    bigTempLayout->addWidget(m_tempValueLabel);

    QLabel *tempUnit = new QLabel("°C", m_topCard);
    tempUnit->setStyleSheet("color: rgba(255,255,255,0.9); font-size: 20px; background: transparent; padding-top: 10px;");
    bigTempLayout->addWidget(tempUnit);
    bigTempLayout->addStretch();

    QLabel *humIcon = new QLabel("💧", m_topCard);
    humIcon->setStyleSheet("font-size: 24px; background: transparent;");
    bigTempLayout->addWidget(humIcon);

    m_humValueLabel = new QLabel("--", m_topCard);
    m_humValueLabel->setStyleSheet("color: white; font-size: 28px; font-weight: bold; background: transparent; padding-top: 8px;");
    bigTempLayout->addWidget(m_humValueLabel);

    QLabel *humUnit = new QLabel("%", m_topCard);
    humUnit->setStyleSheet("color: rgba(255,255,255,0.9); font-size: 16px; background: transparent; padding-top: 12px;");
    bigTempLayout->addWidget(humUnit);

    topLayout->addLayout(bigTempLayout);

    mainLayout->addWidget(m_topCard);

    QWidget *detailCard = new QWidget(this);
    detailCard->setStyleSheet(
        "background-color: #ffffff; border-radius: 16px; margin: 16px 20px 0 20px;"
    );
    QVBoxLayout *detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(20, 20, 20, 20);
    detailLayout->setSpacing(16);

    QLabel *detailTitle = new QLabel("传感器详情", detailCard);
    detailTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #303133;");
    detailLayout->addWidget(detailTitle);

    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(12);

    auto makeDataCard = [&](const QString &icon, const QString &title, const QString &color) -> QWidget* {
        QWidget *card = new QWidget(detailCard);
        card->setMinimumHeight(90);
        card->setStyleSheet(QString(
            "background-color: #f9fafc; border-radius: 12px; border-left: 4px solid %1;"
        ).arg(color));
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 10);
        cardLayout->setSpacing(6);

        QHBoxLayout *headerLayout = new QHBoxLayout();
        QLabel *iconLabel = new QLabel(icon, card);
        iconLabel->setStyleSheet("font-size: 18px; background: transparent; border: none;");
        headerLayout->addWidget(iconLabel);
        QLabel *titleLabel = new QLabel(title, card);
        titleLabel->setStyleSheet(QString("font-size: 12px; color: #909399; background: transparent; border: none; border-left: none;"));
        headerLayout->addWidget(titleLabel);
        headerLayout->addStretch();
        cardLayout->addLayout(headerLayout);

        QLabel *valueLabel = new QLabel("--", card);
        valueLabel->setStyleSheet(QString("font-size: 20px; font-weight: bold; color: %1; background: transparent; border: none;").arg(color));
        cardLayout->addWidget(valueLabel);

        return card;
    };

    QWidget *lightCard = makeDataCard("☀", "光照强度", "#e6a23c");
    QWidget *motionCard = makeDataCard("🚶", "人体感应", "#f56c6c");

    grid->addWidget(lightCard, 0, 0);
    grid->addWidget(motionCard, 0, 1);

    detailLayout->addLayout(grid);

    m_lightValueLabel = lightCard->findChildren<QLabel*>().last();
    m_motionValueLabel = motionCard->findChildren<QLabel*>().last();

    mainLayout->addWidget(detailCard);

    QWidget *bottomBar = new QWidget(this);
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(20, 12, 20, 16);
    bottomLayout->setSpacing(12);

    m_lastUpdateLabel = new QLabel("上次更新: --", bottomBar);
    m_lastUpdateLabel->setStyleSheet("color: #909399; font-size: 12px;");
    bottomLayout->addWidget(m_lastUpdateLabel);
    bottomLayout->addStretch();

    m_refreshButton = new QPushButton("刷新", bottomBar);
    m_refreshButton->setStyleSheet(
        "QPushButton { background-color: #43e97b; color: white; border: none; border-radius: 8px; "
        "padding: 8px 24px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: #38d968; }"
        "QPushButton:pressed { background-color: #2ec85c; }"
    );
    connect(m_refreshButton, &QPushButton::clicked, this, &SensorControl::refreshData);
    bottomLayout->addWidget(m_refreshButton);

    mainLayout->addStretch();
    mainLayout->addWidget(bottomBar);
}

void SensorControl::setDeviceInfo(const QString &roomName, const QString &deviceName)
{
    m_roomName = roomName;
    m_deviceName = deviceName;
    setWindowTitle(deviceName + " - 传感器");
    m_deviceNameLabel->setText(deviceName);

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
    fetchFromDevice();
    m_refreshTimer->start(10000);
}

void SensorControl::loadState()
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        QString state = db.getDeviceState(userId, m_roomName, m_deviceName);
        if (!state.isEmpty()) {
            m_statusLabel->setText(state);
            QRegularExpression re("温度: ([\\d.]+).*湿度: ([\\d.]+).*光照: (\\d+).*人体: (有|无)");
            QRegularExpressionMatch match = re.match(state);
            if (match.hasMatch()) {
                double temp = match.captured(1).toDouble();
                double hum = match.captured(2).toDouble();
                int light = match.captured(3).toInt();
                bool motion = (match.captured(4) == "有");
                updateDisplay(temp, hum, light, motion);
            }
        }
    }
}

void SensorControl::refreshData()
{
    m_refreshButton->setEnabled(false);
    m_refreshButton->setText("刷新中...");
    fetchFromDevice();
    m_refreshButton->setEnabled(true);
    m_refreshButton->setText("刷新");
}

void SensorControl::fetchFromDevice()
{
    if (m_deviceId.isEmpty() || !m_services || !m_services->fetchSensorDataAsync) return;

    double temperature = 0, humidity = 0;
    int light = 0;
    bool motion = false;

    bool ok = m_services->fetchSensorDataAsync(m_deviceId,
                                               temperature, humidity,
                                               light, motion);
    if (ok) {
        updateDisplay(temperature, humidity, light, motion);

        Database db;
        if (db.open()) {
            QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
            QString stateStr = QString("温度: %1°C, 湿度: %2%%, 光照: %3%, 人体: %4")
                                   .arg(temperature, 0, 'f', 1)
                                   .arg(humidity, 0, 'f', 1)
                                   .arg(light)
                                   .arg(motion ? "有" : "无");
            db.updateDeviceState(userId, m_roomName, m_deviceName, stateStr);
            m_statusLabel->setText("在线");
        }
    } else {
        m_statusLabel->setText("等待数据...");
    }

    m_lastUpdateLabel->setText("上次更新: " + QDateTime::currentDateTime().toString("HH:mm:ss"));
}

void SensorControl::updateDisplay(double temperature, double humidity, int light, bool motion)
{
    m_tempValueLabel->setText(QString::number(temperature, 'f', 1));
    m_humValueLabel->setText(QString::number(humidity, 'f', 0));
    m_lightValueLabel->setText(QString("%1%").arg(light));
    m_motionValueLabel->setText(motion ? "有人" : "无人");

    if (temperature > 30) {
        m_topCard->setStyleSheet(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #f56c6c, stop:1 #f78989);"
            "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;"
        );
    } else if (temperature < 10) {
        m_topCard->setStyleSheet(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #409eff, stop:1 #79bbff);"
            "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;"
        );
    } else {
        m_topCard->setStyleSheet(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #43e97b, stop:1 #38f9d7);"
            "border-bottom-left-radius: 24px; border-bottom-right-radius: 24px;"
        );
    }
}
