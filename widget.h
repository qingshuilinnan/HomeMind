#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslConfiguration>
#include <QSslError>
#include <QRegularExpression>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QSet>
#include <QProcess>
#include <QThread>
#include <QListWidget>
#include "database.h"
#include "httpserver.h"
#include "devicediscovery.h"
#include "plugin/deviceplugininterface.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class RoomDetail;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;

private slots:
    void updateTime();
    void on_addRoomButton_clicked();
    void on_deleteRoomButton_clicked();
    void on_changePasswordButton_clicked();
    void on_bindPhoneButton_clicked();
    void on_logoutButton_clicked();
    void on_checkUpdateButton_clicked();
    void on_userManagementButton_clicked();
    void on_locationButton_clicked();
    void fetchLocation();
    void fetchWeather(const QString &city);
    void autoUpdateWeather();
    void on_addTaskButton_clicked();
    void showTaskEditDialog(Database::TaskInfo *info = nullptr);
    void loadTasksFromDatabase();
    void addTaskItem(const Database::TaskInfo &task);
    void processAutomations();

public:
    // 环境数据存取 (供 HttpServer 使用)
    QString getOutdoorTemp() const { return outdoorTemp; }
    QString getWeatherDesc() const { return weatherDesc; }
    QString getIndoorTemp() const { return indoorTemp; }
    QString getIndoorHum() const { return indoorHum; }

private:
    void loadRoomsFromDatabase();
    void loadScenesFromDatabase();
    void loadAllDevicesFromDatabase();
    void addRoomItem(const QString &roomName);
    void addSceneItem(const QString &sceneName);
    void addAllDeviceItem(const QString &deviceName, const QString &roomName, const QString &deviceType, const QString &state);
    void showSceneSettingsDialog(const QString &sceneName);

private:
    Ui::Widget *ui;
    QTimer *timeTimer;
    QTimer *weatherTimer;
    QTimer *automationTimer;
    QNetworkAccessManager *networkManager;
    HttpServer *webServer;
    QDateTime lastRequestTime;

    // 实时环境数据存储
    QString outdoorTemp = "--";
    QString weatherDesc = "未知";
    QString indoorTemp = "--";
    QString indoorHum = "--";

    void initUI();
    void initConnections();
    void updateStatusBar();
    void pollSensorData();

    QTimer *sensorTimer;
    DeviceDiscovery *m_discovery;
    HostServices m_hostServices;

    // 插件管理
    void initPluginTab();
    void refreshPluginList();
    void compilePlugins();
    QListWidget *m_pluginListWidget = nullptr;
    QLabel *m_pluginStatusLabel = nullptr;
    QPushButton *m_pluginCompileBtn = nullptr;
    QPushButton *m_pluginReloadBtn = nullptr;
    QPushButton *m_pluginRemoveBtn = nullptr;
    QStringList m_compileQueue;
    int m_compileIndex = 0;
    QString m_currentBuildDir;
    QString m_buildBaseDir;
    QString m_qmakePath;
    void compileNextInQueue();

private slots:
    void onPluginQmakeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPluginMakeFinished(int exitCode, QProcess::ExitStatus exitStatus);
};
#endif // WIDGET_H
