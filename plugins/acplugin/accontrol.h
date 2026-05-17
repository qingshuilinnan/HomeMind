#ifndef ACCONTROL_H
#define ACCONTROL_H

#include <QWidget>

struct HostServices;

QT_BEGIN_NAMESPACE
namespace Ui {
class ACControl;
}
QT_END_NAMESPACE

class ACControl : public QWidget
{
    Q_OBJECT

public:
    explicit ACControl(QWidget *parent = nullptr);
    ~ACControl() override;
    void setHostServices(HostServices *services);
    void setDeviceInfo(const QString &roomName, const QString &deviceName);
    void loadState();

private slots:
    void on_powerButton_clicked();
    void on_temperatureUpButton_clicked();
    void on_temperatureDownButton_clicked();
    void on_fanSpeedComboBox_currentIndexChanged(int index);
    void on_verticalSwingCheckBox_stateChanged(int arg1);
    void on_horizontalSwingCheckBox_stateChanged(int arg1);
    void on_timerButton_clicked();

private:
    Ui::ACControl *ui;
    HostServices *m_services = nullptr;
    QString m_roomName;
    QString m_deviceName;
    QString m_deviceId;
    bool isPowerOn;
    int currentTemperature;
    void updateTemperatureDisplay();
    void updateUI();
    void saveState();
    void sendCommand(const QJsonObject &cmd);
};
#endif // ACCONTROL_H
