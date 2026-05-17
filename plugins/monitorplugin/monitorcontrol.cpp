#include "monitorcontrol.h"
#include "database.h"
#include "deviceplugininterface.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonObject>
#include <QDateTime>
#include <QMessageBox>

MonitorControl::MonitorControl(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(640, 480);
    setWindowTitle("监控画面");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    m_videoFrame = new QFrame(this);
    m_videoFrame->setFrameStyle(QFrame::Box | QFrame::Sunken);
    m_videoFrame->setStyleSheet(
        "QFrame {"
        "  background-color: #1a1a2e;"
        "  border: 2px solid #16213e;"
        "  border-radius: 8px;"
        "}"
    );
    m_videoFrame->setMinimumSize(620, 360);

    QVBoxLayout *videoLayout = new QVBoxLayout(m_videoFrame);
    videoLayout->setAlignment(Qt::AlignCenter);

    m_videoPlaceholder = new QLabel(m_videoFrame);
    m_videoPlaceholder->setText("\xF0\x9F\x93\xB7\n监控画面未启动");
    m_videoPlaceholder->setAlignment(Qt::AlignCenter);
    m_videoPlaceholder->setStyleSheet(
        "QLabel {"
        "  color: #6c7293;"
        "  font-size: 24px;"
        "  border: none;"
        "  background: transparent;"
        "}"
    );
    videoLayout->addWidget(m_videoPlaceholder);

    mainLayout->addWidget(m_videoFrame);

    QHBoxLayout *statusLayout = new QHBoxLayout();

    m_statusLabel = new QLabel("状态: 未连接", this);
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "  color: #e94560;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 4px 12px;"
        "  background-color: #16213e;"
        "  border-radius: 4px;"
        "}"
    );

    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet(
        "QLabel {"
        "  color: #a0aec0;"
        "  font-size: 13px;"
        "  padding: 4px 12px;"
        "}"
    );

    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_timeLabel);
    mainLayout->addLayout(statusLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    m_startBtn = new QPushButton("\xE2\x96\xB6 开始监控", this);
    m_startBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #0f3460;"
        "  color: white;"
        "  border: none;"
        "  padding: 10px 20px;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1a4a7a;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #2d2d44;"
        "  color: #6c7293;"
        "}"
    );

    m_stopBtn = new QPushButton("\xE2\x9A\xA0 停止监控", this);
    m_stopBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #e94560;"
        "  color: white;"
        "  border: none;"
        "  padding: 10px 20px;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #c23152;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #2d2d44;"
        "  color: #6c7293;"
        "}"
    );

    m_screenshotBtn = new QPushButton("\xF0\x9F\x93\xB8 截图", this);
    m_screenshotBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #533483;"
        "  color: white;"
        "  border: none;"
        "  padding: 10px 20px;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #6c4fa0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #2d2d44;"
        "  color: #6c7293;"
        "}"
    );

    m_fullscreenBtn = new QPushButton("\xE2\x9B\xB6 全屏", this);
    m_fullscreenBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #0f3460;"
        "  color: white;"
        "  border: none;"
        "  padding: 10px 20px;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1a4a7a;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #2d2d44;"
        "  color: #6c7293;"
        "}"
    );

    connect(m_startBtn, &QPushButton::clicked, this, &MonitorControl::onStartClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &MonitorControl::onStopClicked);
    connect(m_screenshotBtn, &QPushButton::clicked, this, &MonitorControl::onScreenshotClicked);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &MonitorControl::onFullscreenClicked);

    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addWidget(m_screenshotBtn);
    btnLayout->addWidget(m_fullscreenBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    m_stopBtn->setEnabled(false);
    m_screenshotBtn->setEnabled(false);
    m_fullscreenBtn->setEnabled(false);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MonitorControl::refreshStatus);

    setStyleSheet(
        "MonitorControl {"
        "  background-color: #1a1a2e;"
        "}"
    );
}

void MonitorControl::setHostServices(HostServices *services)
{
    m_services = services;
}

void MonitorControl::setDeviceInfo(const QString &roomName, const QString &deviceName)
{
    m_roomName = roomName;
    m_deviceName = deviceName;
    setWindowTitle(deviceName + " - 监控画面");

    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        Database::DeviceInfo info = db.getDeviceInfo(userId, roomName, deviceName);
        m_deviceId = info.deviceId;
    }

    loadState();
}

void MonitorControl::loadState()
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        QString state = db.getDeviceState(userId, m_roomName, m_deviceName);
        if (!state.isEmpty()) {
            updateButtonStates(state);
        }
    }
}

void MonitorControl::onStartClicked()
{
    saveState("录制中");
    sendMonitorCommand("start");

    m_videoPlaceholder->setText("\xF0\x9F\x94\xB4\n监控录制中...");
    m_videoPlaceholder->setStyleSheet(
        "QLabel {"
        "  color: #e94560;"
        "  font-size: 24px;"
        "  border: none;"
        "  background: transparent;"
        "}"
    );

    m_statusLabel->setText("状态: 录制中");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "  color: #4ecca3;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 4px 12px;"
        "  background-color: #16213e;"
        "  border-radius: 4px;"
        "}"
    );

    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_screenshotBtn->setEnabled(true);
    m_fullscreenBtn->setEnabled(true);

    m_statusTimer->start(1000);
    refreshStatus();
}

void MonitorControl::onStopClicked()
{
    saveState("关闭");
    sendMonitorCommand("stop");

    m_videoPlaceholder->setText("\xF0\x9F\x93\xB7\n监控画面未启动");
    m_videoPlaceholder->setStyleSheet(
        "QLabel {"
        "  color: #6c7293;"
        "  font-size: 24px;"
        "  border: none;"
        "  background: transparent;"
        "}"
    );

    m_statusLabel->setText("状态: 已停止");
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "  color: #e94560;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 4px 12px;"
        "  background-color: #16213e;"
        "  border-radius: 4px;"
        "}"
    );

    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_screenshotBtn->setEnabled(false);
    m_fullscreenBtn->setEnabled(false);

    m_statusTimer->stop();
    m_timeLabel->clear();
}

void MonitorControl::onScreenshotClicked()
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString filename = m_deviceName + "_screenshot_" + timestamp + ".png";

    QMessageBox::information(this, "截图保存",
        "截图已保存: " + filename + "\n\n"
        "提示: 实际截图功能需要对接视频流接口。");
}

void MonitorControl::onFullscreenClicked()
{
    if (isFullScreen()) {
        showNormal();
        m_fullscreenBtn->setText("\xE2\x9B\xB6 全屏");
    } else {
        showFullScreen();
        m_fullscreenBtn->setText("\xE2\x9B\xB3 退出全屏");
    }
}

void MonitorControl::refreshStatus()
{
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    m_timeLabel->setText("时间: " + currentTime);
}

void MonitorControl::saveState(const QString &state)
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        db.updateDeviceState(userId, m_roomName, m_deviceName, state);
    }
}

void MonitorControl::sendMonitorCommand(const QString &command)
{
    if (!m_services || !m_services->sendCommand || m_deviceId.isEmpty()) return;

    QJsonObject cmd;
    cmd["type"] = "monitor_control";
    cmd["value"] = command;
    m_services->sendCommand(m_deviceId, cmd);
}

void MonitorControl::updateButtonStates(const QString &state)
{
    if (state == "录制中") {
        m_startBtn->setEnabled(false);
        m_stopBtn->setEnabled(true);
        m_screenshotBtn->setEnabled(true);
        m_fullscreenBtn->setEnabled(true);

        m_videoPlaceholder->setText("\xF0\x9F\x94\xB4\n监控录制中...");
        m_videoPlaceholder->setStyleSheet(
            "QLabel {"
            "  color: #e94560;"
            "  font-size: 24px;"
            "  border: none;"
            "  background: transparent;"
            "}"
        );

        m_statusLabel->setText("状态: 录制中");
        m_statusLabel->setStyleSheet(
            "QLabel {"
            "  color: #4ecca3;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "  padding: 4px 12px;"
            "  background-color: #16213e;"
            "  border-radius: 4px;"
            "}"
        );

        m_statusTimer->start(1000);
        refreshStatus();
    } else {
        m_startBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        m_screenshotBtn->setEnabled(false);
        m_fullscreenBtn->setEnabled(false);

        m_videoPlaceholder->setText("\xF0\x9F\x93\xB7\n监控画面未启动");
        m_videoPlaceholder->setStyleSheet(
            "QLabel {"
            "  color: #6c7293;"
            "  font-size: 24px;"
            "  border: none;"
            "  background: transparent;"
            "}"
        );

        m_statusLabel->setText("状态: 已停止");
        m_statusLabel->setStyleSheet(
            "QLabel {"
            "  color: #e94560;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "  padding: 4px 12px;"
            "  background-color: #16213e;"
            "  border-radius: 4px;"
            "}"
        );

        m_statusTimer->stop();
        m_timeLabel->clear();
    }
}
