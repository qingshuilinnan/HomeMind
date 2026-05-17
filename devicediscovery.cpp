#include "devicediscovery.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QDebug>
#include <QDateTime>

DeviceDiscovery *DeviceDiscovery::s_instance = nullptr;

DeviceDiscovery::DeviceDiscovery(QObject *parent)
    : QObject(parent)
    , m_mqtt(new MqttClient(this))
    , m_udpSocket(new QUdpSocket(this))
    , m_udpBroadcastSocket(new QUdpSocket(this))
    , m_cleanupTimer(new QTimer(this))
    , m_broadcastTimer(new QTimer(this))
    , m_brokerPort(MQTT_PORT)
{
    s_instance = this;

    connect(m_mqtt, &MqttClient::connected, this, &DeviceDiscovery::onMqttConnected);
    connect(m_mqtt, &MqttClient::disconnected, this, &DeviceDiscovery::onMqttDisconnected);
    connect(m_mqtt, &MqttClient::messageReceived, this, &DeviceDiscovery::onMqttMessage);

    connect(m_cleanupTimer, &QTimer::timeout, this, &DeviceDiscovery::cleanStaleDevices);
    connect(m_broadcastTimer, &QTimer::timeout, this, &DeviceDiscovery::broadcastBrokerIp);
}

DeviceDiscovery::~DeviceDiscovery()
{
    stopBrokerDiscovery();
    if (s_instance == this) s_instance = nullptr;
}

DeviceDiscovery *DeviceDiscovery::instance()
{
    return s_instance;
}

void DeviceDiscovery::startBrokerDiscovery()
{
    if (m_udpSocket->state() == QAbstractSocket::BoundState) {
        return;
    }

    foreach (const QHostAddress &addr, QNetworkInterface::allAddresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            m_brokerHost = addr.toString();
            break;
        }
    }

    if (m_brokerHost.isEmpty()) {
        qDebug() << "DeviceDiscovery: cannot determine local IP";
        return;
    }

    bool bound = m_udpSocket->bind(QHostAddress::AnyIPv4, UDP_PORT,
                                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!bound) {
        qDebug() << "DeviceDiscovery: UDP bind failed:" << m_udpSocket->errorString();
        return;
    }

    connect(m_udpSocket, &QUdpSocket::readyRead, this, &DeviceDiscovery::onUdpReadyRead);
    m_cleanupTimer->start(CLEANUP_INTERVAL_MS);
    m_broadcastTimer->start(BROKER_BROADCAST_INTERVAL_MS);

    m_mqtt->setClientId("HomeMind_Broker");
    m_mqtt->connectToBroker(m_brokerHost, m_brokerPort);

    broadcastBrokerIp();

    qDebug() << "DeviceDiscovery: broker discovery started, local IP" << m_brokerHost;
}

void DeviceDiscovery::stopBrokerDiscovery()
{
    m_broadcastTimer->stop();
    m_cleanupTimer->stop();
    if (m_udpSocket->state() == QAbstractSocket::BoundState) {
        m_udpSocket->close();
    }
    m_mqtt->disconnectFromBroker();
    m_devices.clear();
}

bool DeviceDiscovery::isBrokerConnected() const
{
    return m_mqtt->isConnected();
}

QString DeviceDiscovery::brokerHost() const
{
    return m_brokerHost;
}

MqttClient *DeviceDiscovery::mqttClient() const
{
    return m_mqtt;
}

QMap<QString, DeviceDiscovery::DiscoveredDevice> DeviceDiscovery::discoveredDevices() const
{
    return m_devices;
}

DeviceDiscovery::DiscoveredDevice DeviceDiscovery::getDevice(const QString &deviceId) const
{
    return m_devices.value(deviceId);
}

QStringList DeviceDiscovery::deviceIdsByType(const QString &type) const
{
    QStringList ids;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().deviceType == type) {
            ids.append(it.key());
        }
    }
    return ids;
}

void DeviceDiscovery::sendUdpBrokerBroadcast()
{
    broadcastBrokerIp();
}

void DeviceDiscovery::requestDeviceScan()
{
    if (!m_mqtt->isConnected()) return;
    m_mqtt->publish("homemind/scan", "{}", 0);
}

void DeviceDiscovery::onMqttConnected()
{
    qDebug() << "DeviceDiscovery: MQTT connected to broker";
    m_mqtt->subscribe("homemind/+/register");
    m_mqtt->subscribe("homemind/+/state");
    m_mqtt->subscribe("homemind/+/offline");
    m_subscribedTopics.clear();
    m_subscribedTopics << "homemind/+/register" << "homemind/+/state" << "homemind/+/offline";

    emit brokerConnected();
}

void DeviceDiscovery::onMqttDisconnected()
{
    qDebug() << "DeviceDiscovery: MQTT disconnected";
    m_subscribedTopics.clear();
    emit brokerDisconnected();
}

void DeviceDiscovery::onMqttMessage(const QString &topic, const QByteArray &payload)
{
    QJsonObject msg = QJsonDocument::fromJson(payload).object();
    QStringList parts = topic.split('/');
    if (parts.size() < 3) return;

    QString deviceId = parts[1];
    QString action = parts[2];

    if (action == "register") {
        handleDeviceRegister(deviceId, msg);
    } else if (action == "state") {
        handleDeviceState(deviceId, msg);
    } else if (action == "offline") {
        if (m_devices.contains(deviceId)) {
            m_devices.remove(deviceId);
            emit deviceLost(deviceId);
        }
    }
}

void DeviceDiscovery::handleDeviceRegister(const QString &deviceId, const QJsonObject &msg)
{
    DiscoveredDevice dev;
    dev.deviceId = deviceId;
    dev.ip = msg["ip"].toString();
    dev.tcpPort = msg["tcp_port"].toInt(0);
    dev.deviceType = msg["device_type"].toString();
    dev.deviceName = msg["device_name"].toString(deviceId);
    dev.lastSeen = QDateTime::currentMSecsSinceEpoch();

    bool isNew = !m_devices.contains(deviceId);
    m_devices[deviceId] = dev;

    if (isNew) {
        qDebug() << "DeviceDiscovery: registered" << dev.deviceType << dev.deviceName
                 << "(" << deviceId << ") at" << dev.ip;
        emit deviceFound(deviceId);
    }
}

void DeviceDiscovery::handleDeviceState(const QString &deviceId, const QJsonObject &msg)
{
    if (!m_devices.contains(deviceId)) {
        DiscoveredDevice dev;
        dev.deviceId = deviceId;
        dev.ip = msg["ip"].toString();
        dev.deviceType = msg["device_type"].toString();
        dev.deviceName = msg["device_name"].toString(deviceId);
        dev.lastSeen = QDateTime::currentMSecsSinceEpoch();
        m_devices[deviceId] = dev;
        emit deviceFound(deviceId);
    } else {
        m_devices[deviceId].lastSeen = QDateTime::currentMSecsSinceEpoch();
        if (!msg["ip"].toString().isEmpty()) {
            m_devices[deviceId].ip = msg["ip"].toString();
        }
    }

    emit deviceStateChanged(deviceId, msg);
}

void DeviceDiscovery::onUdpReadyRead()
{
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(datagram.data(), datagram.size());

        QJsonObject obj = QJsonDocument::fromJson(datagram).object();
        if (obj["type"].toString() == "mqtt_discover_response") {
            QString ip = obj["broker_ip"].toString();
            if (!ip.isEmpty() && !m_devices.contains("__broker_response__")) {
                qDebug() << "DeviceDiscovery: broker response from" << ip;
            }
        }
    }
}

void DeviceDiscovery::cleanStaleDevices()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList staleIds;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (now - it.value().lastSeen > DEVICE_TIMEOUT_MS) {
            staleIds.append(it.key());
        }
    }
    for (const QString &id : staleIds) {
        m_devices.remove(id);
        emit deviceLost(id);
    }
}

void DeviceDiscovery::broadcastBrokerIp()
{
    if (m_brokerHost.isEmpty()) return;

    QJsonObject msg;
    msg["type"] = "mqtt_discover";
    msg["broker_ip"] = m_brokerHost;
    msg["broker_port"] = m_brokerPort;
    msg["mqtt_topic_prefix"] = "homemind";
    QByteArray datagram = QJsonDocument(msg).toJson(QJsonDocument::Compact);

    m_udpBroadcastSocket->writeDatagram(datagram, QHostAddress::Broadcast, UDP_PORT);
}
