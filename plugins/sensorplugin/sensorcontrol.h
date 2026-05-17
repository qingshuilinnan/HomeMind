#ifndef SENSORCONTROL_H
#define SENSORCONTROL_H

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>

struct HostServices;

class SensorControl : public QWidget
{
    Q_OBJECT

public:
    explicit SensorControl(QWidget *parent = nullptr);
    ~SensorControl() override;
    void setHostServices(HostServices *services);
    void setDeviceInfo(const QString &roomName, const QString &deviceName);
    void loadState();
    void refreshData();

private:
    void buildUI();
    void updateDisplay(double temperature, double humidity, int light, bool motion);
    void fetchFromDevice();

    HostServices *m_services = nullptr;
    QString m_roomName;
    QString m_deviceName;
    QString m_deviceId;

    QLabel *m_deviceNameLabel;
    QLabel *m_statusLabel;
    QLabel *m_tempValueLabel;
    QLabel *m_humValueLabel;
    QLabel *m_lightValueLabel;
    QLabel *m_motionValueLabel;
    QLabel *m_lastUpdateLabel;
    QPushButton *m_refreshButton;

    QWidget *m_topCard;
    QTimer *m_refreshTimer;
};

#endif // SENSORCONTROL_H
