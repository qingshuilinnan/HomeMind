#ifndef DEVICEPLUGININTERFACE_H
#define DEVICEPLUGININTERFACE_H

#include <QtPlugin>
#include <QWidget>
#include <QString>
#include <QJsonObject>
#include <QList>

class Database;
class DeviceDiscovery;

// Function pointer types for cross-plugin operations
typedef bool (*SendCommandFunc)(const QString &deviceId, const QJsonObject &cmd);
typedef bool (*FetchSensorDataAsyncFunc)(const QString &deviceId, double &temperature, double &humidity, int &light, bool &motion);
typedef void (*CacheSensorDataFunc)(const QString &deviceId, const QJsonObject &data);

struct HostServices {
    Database *db;
    DeviceDiscovery *discovery;
    QString currentUsername;
    SendCommandFunc sendCommand;
    FetchSensorDataAsyncFunc fetchSensorDataAsync;
    CacheSensorDataFunc cacheSensorData;
};

class DevicePluginInterface
{
public:
    virtual ~DevicePluginInterface() {}

    // Metadata
    virtual QString deviceType() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const = 0;
    virtual QString iconText() const = 0;
    virtual QString mqttDeviceType() const = 0;

    // Lifecycle
    virtual void initialize(HostServices *services) = 0;

    // Widget factory - creates a fully initialized control widget
    virtual QWidget *createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent = nullptr) = 0;

    // Command building - given a state string, produce MQTT command sequence
    virtual QList<QJsonObject> buildCommands(const QString &state) const = 0;

    // Scene state editor - create an editor widget for scene configuration
    virtual QWidget *createSceneStateEditor(QWidget *parent = nullptr) = 0;
    virtual QString sceneEditorResult(QWidget *editor) const = 0;

    // State display helpers
    virtual QString stateDisplayText(const QString &state) const = 0;

    // Device add dialog support
    virtual bool supportsScan() const { return false; }
    virtual bool showAddDeviceDialog(QWidget *parent, const QString &roomName, const QString &username) { Q_UNUSED(parent); Q_UNUSED(roomName); Q_UNUSED(username); return false; }
    virtual bool showManualAddDialog(QWidget *parent, const QString &roomName, const QString &username) { Q_UNUSED(parent); Q_UNUSED(roomName); Q_UNUSED(username); return false; }
};

#define DevicePluginInterface_iid "com.homemind.DevicePluginInterface/1.0"
Q_DECLARE_INTERFACE(DevicePluginInterface, DevicePluginInterface_iid)

#endif // DEVICEPLUGININTERFACE_H
