#ifndef DEVICECOMMANDER_H
#define DEVICECOMMANDER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QTimer>

class DeviceCommander : public QObject
{
    Q_OBJECT
public:
    explicit DeviceCommander(QObject *parent = nullptr);
    ~DeviceCommander();

    struct SensorData {
        double temperature = 0;
        double humidity = 0;
        int light = 0;
        bool motion = false;
        qint64 timestamp = 0;
    };

    static bool sendCommand(const QString &deviceId, const QJsonObject &cmd);
    static bool applyDeviceState(int userId, const QString &room, const QString &name,
                                 const QString &deviceType, const QString &state);
    static bool fetchSensorData(const QString &ip, int port,
                                double &temperature, double &humidity,
                                int &light, bool &motion);
    static bool fetchSensorDataAsync(const QString &deviceId,
                                     double &temperature, double &humidity,
                                     int &light, bool &motion);
    static void cacheSensorData(const QString &deviceId, const QJsonObject &data);
    static SensorData getCachedSensorData(const QString &deviceId);

private:
    static QMap<QString, SensorData> s_sensorCache;
};

#endif
