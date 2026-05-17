#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QObject>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include "database.h"

class Widget; // 前向声明

class HttpServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit HttpServer(Widget *widget, QObject *parent = nullptr);
    bool start(quint16 port = 8080);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

signals:
    void dataChanged();

private slots:
    void handleRequest();

private:
    void sendResponse(QTcpSocket *socket, const QByteArray &content, const QString &contentType = "text/html");
    void sendJsonResponse(QTcpSocket *socket, const QJsonObject &response);
    void handleApiRequest(QTcpSocket *socket, const QString &path, const QString &method, const QByteArray &body, const QString &authHeader);

    // Session management
    QString generateToken();
    QString validateToken(const QString &token);
    QString extractToken(const QString &authHeader);

    // API handlers
    void handleLogin(QTcpSocket *socket, const QByteArray &body);
    void handleRegister(QTcpSocket *socket, const QByteArray &body);
    void handleLogout(QTcpSocket *socket, const QString &token);
    void handleChangePassword(QTcpSocket *socket, const QByteArray &body, const QString &username);
    void handleStatus(QTcpSocket *socket, const QString &username);
    void handleDevices(QTcpSocket *socket, int userId);
    void handleTasks(QTcpSocket *socket, int userId);
    void handleRooms(QTcpSocket *socket, int userId);
    void handleScenes(QTcpSocket *socket, int userId);
    void handleApplyScene(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleControl(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleCreateRoom(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleDeleteRoom(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleCreateDevice(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleDeleteDevice(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleCreateScene(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleDeleteScene(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleSceneDevices(QTcpSocket *socket, const QString &sceneName, int userId);
    void handleSaveSceneDevices(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleCreateTask(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleUpdateTask(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleDeleteTask(QTcpSocket *socket, const QByteArray &body, int userId);
    void handleUsers(QTcpSocket *socket, const QString &username);
    void handleDeleteUser(QTcpSocket *socket, const QByteArray &body, const QString &username);

    Database db;
    Widget *mainWidget;
    QMap<QString, QString> m_sessions; // token -> username
};

#endif // HTTPSERVER_H
