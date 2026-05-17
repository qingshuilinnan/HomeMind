#include "devicecommander.h"
#include "devicediscovery.h"
#include "database.h"
#include "loginwindow.h"
#include "plugin/pluginmanager.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QDebug>
#include <QDateTime>

QMap<QString, DeviceCommander::SensorData> DeviceCommander::s_sensorCache;

DeviceCommander::DeviceCommander(QObject *parent)
    : QObject(parent)
{
}

DeviceCommander::~DeviceCommander()
{
}

bool DeviceCommander::sendCommand(const QString &deviceId, const QJsonObject &cmd)
{
    DeviceDiscovery *discovery = DeviceDiscovery::instance();
    if (!discovery || !discovery->isBrokerConnected()) {
        qDebug() << "DeviceCommander: broker not connected";
        return false;
    }

    DeviceDiscovery::DiscoveredDevice dev = discovery->getDevice(deviceId);
    if (dev.deviceId.isEmpty()) {
        qDebug() << "DeviceCommander: device not found:" << deviceId;
        return false;
    }

    QString topic = "homemind/" + deviceId + "/cmd";
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);

    bool ok = discovery->mqttClient()->publish(topic, payload, 1);
    qDebug() << "DeviceCommander: publish" << topic << payload << (ok ? "OK" : "FAIL");
    return ok;
}

bool DeviceCommander::applyDeviceState(int userId, const QString &room, const QString &name,
                                       const QString &deviceType, const QString &state)
{
    Database db;
    if (!db.open()) return false;

    Database::DeviceInfo info = db.getDeviceInfo(userId, room, name);
    if (info.deviceId.isEmpty()) {
        qDebug() << "DeviceCommander: no device_id for" << name;
        return false;
    }

    db.updateDeviceState(userId, room, name, state);

    DevicePluginInterface *plugin = PluginManager::instance()->pluginForType(deviceType);
    if (plugin) {
        QList<QJsonObject> commands = plugin->buildCommands(state);
        for (const auto &cmd : commands) {
            sendCommand(info.deviceId, cmd);
        }
    }

    return true;
}

bool DeviceCommander::fetchSensorData(const QString &ip, int port,
                                      double &temperature, double &humidity,
                                      int &light, bool &motion)
{
    Q_UNUSED(ip);
    Q_UNUSED(port);
    Q_UNUSED(temperature);
    Q_UNUSED(humidity);
    Q_UNUSED(light);
    Q_UNUSED(motion);
    return false;
}

bool DeviceCommander::fetchSensorDataAsync(const QString &deviceId,
                                           double &temperature, double &humidity,
                                           int &light, bool &motion)
{
    auto it = s_sensorCache.find(deviceId);
    if (it == s_sensorCache.end()) return false;

    const SensorData &data = it.value();
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - data.timestamp;
    if (elapsed > 30000) return false;

    temperature = data.temperature;
    humidity = data.humidity;
    light = data.light;
    motion = data.motion;
    return true;
}

void DeviceCommander::cacheSensorData(const QString &deviceId, const QJsonObject &data)
{
    SensorData sd;
    sd.temperature = data["temperature"].toDouble();
    sd.humidity = data["humidity"].toDouble();
    sd.light = data["light"].toInt();
    sd.motion = data["motion"].toBool();
    sd.timestamp = QDateTime::currentMSecsSinceEpoch();
    s_sensorCache[deviceId] = sd;
}

DeviceCommander::SensorData DeviceCommander::getCachedSensorData(const QString &deviceId)
{
    return s_sensorCache.value(deviceId);
}
