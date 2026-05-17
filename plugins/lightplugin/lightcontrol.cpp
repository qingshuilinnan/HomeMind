#include "lightcontrol.h"
#include "ui_lightcontrol.h"
#include "database.h"
#include "deviceplugininterface.h"
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QDialog>

LightControl::LightControl(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LightControl)
    , isPowerOn(false)
{
    ui->setupUi(this);
}

LightControl::~LightControl()
{
    delete ui;
}

void LightControl::setHostServices(HostServices *services)
{
    m_services = services;
}

void LightControl::setDeviceInfo(const QString &roomName, const QString &deviceName)
{
    m_roomName = roomName;
    m_deviceName = deviceName;
    setWindowTitle(deviceName + " - 灯光控制");
    loadState();
}

void LightControl::loadState()
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        QString state = db.getDeviceState(userId, m_roomName, m_deviceName);
        if (!state.isEmpty()) {
            isPowerOn = (state == "开启");
            ui->powerButton->setText(isPowerOn ? "关闭" : "开启");
        }
    }
}

void LightControl::saveState()
{
    if (m_deviceName.isEmpty()) return;
    
    QString state = isPowerOn ? "开启" : "关闭";
    
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        db.updateDeviceState(userId, m_roomName, m_deviceName, state);
    }
}

void LightControl::on_powerButton_clicked()
{
    isPowerOn = !isPowerOn;
    ui->powerButton->setText(isPowerOn ? "关闭" : "开启");
    saveState();
}

void LightControl::on_timerButton_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("设置定时");
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    QTimeEdit *timeEdit = new QTimeEdit(&dialog);
    timeEdit->setDisplayFormat("HH:mm");
    layout->addWidget(timeEdit);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    
    dialog.exec();
}