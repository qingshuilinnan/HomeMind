#include "mqttclient.h"
#include <QJsonDocument>
#include <QDebug>

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_pingTimer(new QTimer(this))
{
    m_clientId = QString("HomeMind_%1").arg(QDateTime::currentMSecsSinceEpoch() % 100000);

    connect(m_socket, &QTcpSocket::connected, this, &MqttClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &MqttClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &MqttClient::onSocketReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &MqttClient::onSocketError);
    connect(m_pingTimer, &QTimer::timeout, this, &MqttClient::sendPingreq);
}

MqttClient::~MqttClient()
{
    disconnectFromBroker();
}

void MqttClient::setClientId(const QString &id)
{
    m_clientId = id;
}

void MqttClient::setKeepAlive(quint16 secs)
{
    m_keepAlive = secs;
}

void MqttClient::setWillMessage(const QString &topic, const QByteArray &payload)
{
    m_will.topic = topic;
    m_will.payload = payload;
}

bool MqttClient::connectToBroker(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_socket->connectToHost(host, port);
    return true;
}

void MqttClient::disconnectFromBroker()
{
    if (m_connected) {
        quint8 header[] = {DISCONNECT, 0x00};
        m_socket->write((char *)header, 2);
        m_connected = false;
    }
    m_pingTimer->stop();
    m_buffer.clear();
    m_socket->disconnectFromHost();
}

bool MqttClient::isConnected() const
{
    return m_connected;
}

void MqttClient::onSocketConnected()
{
    sendConnect();
}

void MqttClient::onSocketDisconnected()
{
    m_connected = false;
    m_pingTimer->stop();
    m_buffer.clear();
    emit disconnected();
}

void MqttClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit errorOccurred(m_socket->errorString());
}

void MqttClient::onSocketReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processData();
}

void MqttClient::sendConnect()
{
    QByteArray variable;
    variable.append(encodeString("MQTT"));
    variable.append('\x04');

    quint8 flags = 0x02;
    if (m_keepAlive > 0) flags |= 0x02;
    if (!m_will.topic.isEmpty()) {
        flags |= 0x04;
        flags |= 0x80;
    }
    variable.append((char)flags);

    variable.append((char)(m_keepAlive >> 8));
    variable.append((char)(m_keepAlive & 0xFF));

    variable.append(encodeString(m_clientId));

    if (!m_will.topic.isEmpty()) {
        variable.append(encodeString(m_will.topic));
        variable.append((char)(m_will.payload.size() >> 8));
        variable.append((char)(m_will.payload.size() & 0xFF));
        variable.append(m_will.payload);
    }

    quint8 type = CONNECT;
    QByteArray packet;
    packet.append((char)type);
    packet.append(encodeLength(variable.size()));
    packet.append(variable);

    m_socket->write(packet);
}

void MqttClient::processData()
{
    while (m_buffer.size() >= 2) {
        int multiplier = 1;
        int remainingLength = 0;
        int idx = 1;
        quint8 encodedByte;
        do {
            if (idx >= m_buffer.size()) return;
            encodedByte = (quint8)m_buffer[idx++];
            remainingLength += (encodedByte & 0x7F) * multiplier;
            multiplier *= 128;
            if (multiplier > 128 * 128 * 128) return;
        } while ((encodedByte & 0x80) != 0);

        int totalLen = idx + remainingLength;
        if (m_buffer.size() < totalLen) return;

        quint8 headerByte = (quint8)m_buffer[0];
        quint8 type = headerByte & 0xF0;
        QByteArray packetData = m_buffer.mid(idx, remainingLength);
        m_buffer.remove(0, totalLen);

        processPacket(type, headerByte, packetData);
    }
}

void MqttClient::processPacket(quint8 type, quint8 headerByte, const QByteArray &data)
{
    switch (type) {
    case CONNACK:
        if (data.size() >= 2 && (quint8)data[1] == 0x00) {
            m_connected = true;
            if (m_keepAlive > 0) {
                m_pingTimer->start(m_keepAlive * 500);
            }
            emit connected();
        } else {
            quint8 retCode = data.size() >= 2 ? (quint8)data[1] : 0xFF;
            emit errorOccurred(QString("CONNACK refused: %1").arg(retCode));
        }
        break;

    case PUBLISH: {
        quint8 qos = (headerByte >> 1) & 0x03;
        int offset = 0;
        QString topic = decodeString(data, 0, offset);
        if (topic.isEmpty()) break;

        if (qos > 0 && offset + 2 <= data.size()) {
            offset += 2;
        }

        QByteArray payload = data.mid(offset);
        emit messageReceived(topic, payload);
        break;
    }

    case PUBACK:
    case SUBACK:
    case UNSUBACK:
    case PINGRESP:
        break;

    default:
        break;
    }
}

bool MqttClient::publish(const QString &topic, const QByteArray &payload, quint8 qos)
{
    if (!m_connected) return false;

    QByteArray variable;
    variable.append(encodeString(topic));

    quint16 pid = 0;
    if (qos > 0) {
        pid = nextPacketId();
        variable.append((char)(pid >> 8));
        variable.append((char)(pid & 0xFF));
    }

    variable.append(payload);

    quint8 type = PUBLISH | (qos << 1);
    QByteArray packet;
    packet.append((char)type);
    packet.append(encodeLength(variable.size()));
    packet.append(variable);

    return m_socket->write(packet) > 0;
}

bool MqttClient::subscribe(const QString &topic, quint8 qos)
{
    if (!m_connected) return false;

    QByteArray variable;
    quint16 pid = nextPacketId();
    variable.append((char)(pid >> 8));
    variable.append((char)(pid & 0xFF));
    variable.append(encodeString(topic));
    variable.append((char)qos);

    QByteArray packet;
    packet.append((char)SUBSCRIBE | 0x02);
    packet.append(encodeLength(variable.size()));
    packet.append(variable);

    return m_socket->write(packet) > 0;
}

bool MqttClient::unsubscribe(const QString &topic)
{
    if (!m_connected) return false;

    QByteArray variable;
    quint16 pid = nextPacketId();
    variable.append((char)(pid >> 8));
    variable.append((char)(pid & 0xFF));
    variable.append(encodeString(topic));

    QByteArray packet;
    packet.append((char)UNSUBSCRIBE | 0x02);
    packet.append(encodeLength(variable.size()));
    packet.append(variable);

    return m_socket->write(packet) > 0;
}

void MqttClient::sendPingreq()
{
    if (m_connected) {
        quint8 header[] = {PINGREQ, 0x00};
        m_socket->write((char *)header, 2);
    }
}

quint16 MqttClient::nextPacketId()
{
    if (++m_packetId == 0) m_packetId = 1;
    return m_packetId;
}

QByteArray MqttClient::encodeLength(int length)
{
    QByteArray encoded;
    do {
        quint8 encodedByte = length % 128;
        length /= 128;
        if (length > 0) encodedByte |= 0x80;
        encoded.append((char)encodedByte);
    } while (length > 0);
    return encoded;
}

QByteArray MqttClient::encodeString(const QString &str)
{
    QByteArray utf8 = str.toUtf8();
    QByteArray result;
    result.append((char)(utf8.size() >> 8));
    result.append((char)(utf8.size() & 0xFF));
    result.append(utf8);
    return result;
}

QString MqttClient::decodeString(const QByteArray &data, int offset, int &newOffset)
{
    if (offset + 2 > data.size()) { newOffset = offset; return ""; }
    int len = ((quint8)data[offset] << 8) | (quint8)data[offset + 1];
    if (offset + 2 + len > data.size()) { newOffset = offset; return ""; }
    newOffset = offset + 2 + len;
    return QString::fromUtf8(data.mid(offset + 2, len));
}
