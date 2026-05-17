#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <functional>

class MqttClient : public QObject
{
    Q_OBJECT
public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient();

    bool connectToBroker(const QString &host, quint16 port = 1883);
    void disconnectFromBroker();
    bool isConnected() const;

    void setClientId(const QString &id);
    void setKeepAlive(quint16 secs);
    void setWillMessage(const QString &topic, const QByteArray &payload);

    bool publish(const QString &topic, const QByteArray &payload, quint8 qos = 0);
    bool subscribe(const QString &topic, quint8 qos = 0);
    bool unsubscribe(const QString &topic);

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &errorString);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void sendPingreq();

private:
    enum PacketType : quint8 {
        CONNECT     = 0x10,
        CONNACK     = 0x20,
        PUBLISH     = 0x30,
        PUBACK      = 0x40,
        SUBSCRIBE   = 0x80,
        SUBACK      = 0x90,
        UNSUBSCRIBE = 0xA0,
        UNSUBACK    = 0xB0,
        PINGREQ     = 0xC0,
        PINGRESP    = 0xD0,
        DISCONNECT  = 0xE0
    };

    void sendConnect();
    void processData();
    void processPacket(quint8 type, quint8 headerByte, const QByteArray &data);
    quint16 nextPacketId();

    static QByteArray encodeLength(int length);
    static QByteArray encodeString(const QString &str);
    static QString decodeString(const QByteArray &data, int offset, int &newOffset);

    QTcpSocket *m_socket;
    QTimer *m_pingTimer;
    QString m_clientId;
    QString m_host;
    quint16 m_port = 1883;
    quint16 m_keepAlive = 60;
    quint16 m_packetId = 0;
    QByteArray m_buffer;
    bool m_connected = false;

    struct WillMsg {
        QString topic;
        QByteArray payload;
    };
    WillMsg m_will;
};

#endif
