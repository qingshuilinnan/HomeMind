#ifndef MONITORCONTROL_H
#define MONITORCONTROL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QTimer>

struct HostServices;

class MonitorControl : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorControl(QWidget *parent = nullptr);
    void setHostServices(HostServices *services);
    void setDeviceInfo(const QString &roomName, const QString &deviceName);
    void loadState();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onScreenshotClicked();
    void onFullscreenClicked();
    void refreshStatus();

private:
    void saveState(const QString &state);
    void sendMonitorCommand(const QString &command);
    void updateButtonStates(const QString &state);

    HostServices *m_services = nullptr;
    QString m_roomName;
    QString m_deviceName;
    QString m_deviceId;

    QFrame *m_videoFrame;
    QLabel *m_videoPlaceholder;
    QLabel *m_statusLabel;
    QLabel *m_timeLabel;
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_screenshotBtn;
    QPushButton *m_fullscreenBtn;
    QTimer *m_statusTimer;
};

#endif // MONITORCONTROL_H
