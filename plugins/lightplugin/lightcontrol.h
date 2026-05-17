#ifndef LIGHTCONTROL_H
#define LIGHTCONTROL_H

#include <QWidget>

struct HostServices;

QT_BEGIN_NAMESPACE
namespace Ui {
class LightControl;
}
QT_END_NAMESPACE

class LightControl : public QWidget
{
    Q_OBJECT

public:
    explicit LightControl(QWidget *parent = nullptr);
    ~LightControl() override;
    void setHostServices(HostServices *services);
    void setDeviceInfo(const QString &roomName, const QString &deviceName);
    void loadState();

private slots:
    void on_powerButton_clicked();
    void on_timerButton_clicked();

private:
    Ui::LightControl *ui;
    HostServices *m_services = nullptr;
    QString m_roomName;
    QString m_deviceName;
    bool isPowerOn;
    void saveState();
};
#endif // LIGHTCONTROL_H