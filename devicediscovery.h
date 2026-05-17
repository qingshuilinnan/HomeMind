#ifndef DEVICEDISCOVERY_H
#define DEVICEDISCOVERY_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QMap>
#include <QJsonObject>
#include <QSet>
#include "mqttclient.h"

class DeviceDiscovery : public QObject
{
    Q_OBJECT

public:
    struct DiscoveredDevice {
        QString deviceId;
        QString ip;
        int tcpPort;
        QString deviceType;
        QString deviceName;
        qint64 lastSeen;
    };

    explicit DeviceDiscovery(QObject *parent = nullptr);
    ~DeviceDiscovery();

    void startBrokerDiscovery();
    void stopBrokerDiscovery();

    bool isBrokerConnected() const;
    QString brokerHost() const;
    MqttClient *mqttClient() const;

    QMap<QString, DiscoveredDevice> discoveredDevices() const;
    DiscoveredDevice getDevice(const QString &deviceId) const;
    QStringList deviceIdsByType(const QString &type) const;

    static DeviceDiscovery *instance();

signals:
    void brokerConnected();
    void brokerDisconnected();
    void deviceFound(const QString &deviceId);
    void deviceLost(const QString &deviceId);
    void deviceStateChanged(const QString &deviceId, const QJsonObject &state);

public slots:
    void sendUdpBrokerBroadcast();
    void requestDeviceScan();

private slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttMessage(const QString &topic, const QByteArray &payload);
    void onUdpReadyRead();
    void cleanStaleDevices();
    void broadcastBrokerIp();

private:
    void handleDeviceRegister(const QString &deviceId, const QJsonObject &msg);
    void handleDeviceState(const QString &deviceId, const QJsonObject &msg);

    MqttClient *m_mqtt;
    QUdpSocket *m_udpSocket;
    QUdpSocket *m_udpBroadcastSocket;
    QTimer *m_cleanupTimer;
    QTimer *m_broadcastTimer;
    QString m_brokerHost;
    quint16 m_brokerPort;
    QMap<QString, DiscoveredDevice> m_devices;
    QSet<QString> m_subscribedTopics;

    static const int UDP_PORT = 8888;
    static const int DEVICE_TIMEOUT_MS = 15000;
    static const int CLEANUP_INTERVAL_MS = 3000;
    static const int BROKER_BROADCAST_INTERVAL_MS = 5000;
    static const int MQTT_PORT = 1883;

    static DeviceDiscovery *s_instance;
};

#endif
