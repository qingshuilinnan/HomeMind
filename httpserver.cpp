#include "httpserver.h"
#include "loginwindow.h"
#include "widget.h"
#include "devicecommander.h"
#include <QUrlQuery>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QUuid>

HttpServer::HttpServer(Widget *widget, QObject *parent) : QTcpServer(parent), mainWidget(widget)
{
    db.open();
}

bool HttpServer::start(quint16 port)
{
    if (!listen(QHostAddress::Any, port)) {
        qDebug() << "HttpServer failed to start on port" << port;
        return false;
    }
    qDebug() << "HttpServer started on port" << port;
    return true;
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);
    connect(socket, &QTcpSocket::readyRead, this, &HttpServer::handleRequest);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}

// --- Session Management ---

QString HttpServer::generateToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString HttpServer::extractToken(const QString &authHeader)
{
    if (authHeader.startsWith("Bearer ")) {
        return authHeader.mid(7).trimmed();
    }
    return "";
}

QString HttpServer::validateToken(const QString &token)
{
    if (token.isEmpty()) return "";
    auto it = m_sessions.find(token);
    if (it != m_sessions.end()) {
        return it.value();
    }
    return "";
}

// --- Response Helpers ---

void HttpServer::sendResponse(QTcpSocket *socket, const QByteArray &content, const QString &contentType)
{
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: " + contentType.toUtf8() + "; charset=utf-8\r\n");
    response.append("Content-Length: " + QByteArray::number(content.size()) + "\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    response.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(content);

    socket->write(response);
    socket->disconnectFromHost();
}

void HttpServer::sendJsonResponse(QTcpSocket *socket, const QJsonObject &response)
{
    sendResponse(socket, QJsonDocument(response).toJson(), "application/json");
}

// --- Request Handling ---

void HttpServer::handleRequest()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray data = socket->readAll();
    QString request = QString::fromUtf8(data);

    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) return;

    QStringList firstLine = lines[0].split(" ");
    if (firstLine.size() < 2) return;

    QString method = firstLine[0];
    QString path = firstLine[1];

    // Extract body
    QByteArray body;
    int bodyIndex = data.indexOf("\r\n\r\n");
    if (bodyIndex != -1) {
        body = data.mid(bodyIndex + 4);
    }

    // Extract Authorization header
    QString authHeader;
    for (const QString &line : lines) {
        if (line.startsWith("Authorization:", Qt::CaseInsensitive)) {
            authHeader = line.mid(14).trimmed();
            break;
        }
    }

    if (path.startsWith("/api/")) {
        if (method == "OPTIONS") {
            sendResponse(socket, "", "text/plain");
            return;
        }
        handleApiRequest(socket, path, method, body, authHeader);
    } else {
        // Static file serving
        QString fileName = path == "/" ? "index.html" : path.mid(1);
        // Try relative to application directory first, then fallback to hardcoded path
        QString appDir = QCoreApplication::applicationDirPath();
        QFile file(appDir + "/web/" + fileName);
        if (!file.exists()) {
            file.setFileName(appDir + "/../web/" + fileName);
        }
        if (!file.exists()) {
            file.setFileName("/home/tohka/HomeMind/web/" + fileName);
        }

        if (file.open(QIODevice::ReadOnly)) {
            QString contentType = "text/plain";
            if (fileName.endsWith(".html")) contentType = "text/html";
            else if (fileName.endsWith(".css")) contentType = "text/css";
            else if (fileName.endsWith(".js")) contentType = "application/javascript";
            else if (fileName.endsWith(".png")) contentType = "image/png";
            else if (fileName.endsWith(".jpg") || fileName.endsWith(".jpeg")) contentType = "image/jpeg";
            else if (fileName.endsWith(".svg")) contentType = "image/svg+xml";
            else if (fileName.endsWith(".ico")) contentType = "image/x-icon";

            sendResponse(socket, file.readAll(), contentType);
        } else {
            sendResponse(socket, "404 Not Found", "text/plain");
        }
    }
}

void HttpServer::handleApiRequest(QTcpSocket *socket, const QString &path, const QString &method, const QByteArray &body, const QString &authHeader)
{
    // Public endpoints (no auth required)
    if (path == "/api/login" && method == "POST") {
        handleLogin(socket, body);
        return;
    }
    if (path == "/api/register" && method == "POST") {
        handleRegister(socket, body);
        return;
    }

    // Validate token for all other endpoints
    QString token = extractToken(authHeader);
    QString username = validateToken(token);
    if (username.isEmpty()) {
        QJsonObject resp;
        resp["success"] = false;
        resp["error"] = "未登录或登录已过期";
        sendJsonResponse(socket, resp);
        return;
    }

    int userId = db.getUserId(username);

    // Authenticated endpoints
    if (path == "/api/logout" && method == "POST") {
        handleLogout(socket, token);
    } else if (path == "/api/status") {
        handleStatus(socket, username);
    } else if (path == "/api/devices") {
        handleDevices(socket, userId);
    } else if (path == "/api/tasks") {
        handleTasks(socket, userId);
    } else if (path == "/api/rooms") {
        handleRooms(socket, userId);
    } else if (path == "/api/scenes") {
        handleScenes(socket, userId);
    } else if (path == "/api/apply_scene" && method == "POST") {
        handleApplyScene(socket, body, userId);
    } else if (path == "/api/control" && method == "POST") {
        handleControl(socket, body, userId);
    } else if (path == "/api/change_password" && method == "POST") {
        handleChangePassword(socket, body, username);
    } else if (path == "/api/rooms/create" && method == "POST") {
        handleCreateRoom(socket, body, userId);
    } else if (path == "/api/rooms/delete" && method == "POST") {
        handleDeleteRoom(socket, body, userId);
    } else if (path == "/api/devices/create" && method == "POST") {
        handleCreateDevice(socket, body, userId);
    } else if (path == "/api/devices/delete" && method == "POST") {
        handleDeleteDevice(socket, body, userId);
    } else if (path == "/api/scenes/create" && method == "POST") {
        handleCreateScene(socket, body, userId);
    } else if (path == "/api/scenes/delete" && method == "POST") {
        handleDeleteScene(socket, body, userId);
    } else if (path.startsWith("/api/scenes/devices") && method == "GET") {
        QString sceneName = QUrlQuery(path).queryItemValue("name");
        handleSceneDevices(socket, sceneName, userId);
    } else if (path == "/api/scenes/devices/save" && method == "POST") {
        handleSaveSceneDevices(socket, body, userId);
    } else if (path == "/api/tasks/create" && method == "POST") {
        handleCreateTask(socket, body, userId);
    } else if (path == "/api/tasks/update" && method == "POST") {
        handleUpdateTask(socket, body, userId);
    } else if (path == "/api/tasks/delete" && method == "POST") {
        handleDeleteTask(socket, body, userId);
    } else if (path == "/api/users") {
        handleUsers(socket, username);
    } else if (path == "/api/users/delete" && method == "POST") {
        handleDeleteUser(socket, body, username);
    } else {
        QJsonObject resp;
        resp["success"] = false;
        resp["error"] = "Unknown endpoint";
        sendJsonResponse(socket, resp);
    }
}

// --- API Handlers ---

void HttpServer::handleLogin(QTcpSocket *socket, const QByteArray &body)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString username = input["username"].toString();
    QString password = input["password"].toString();

    QJsonObject resp;
    if (db.loginUser(username, password)) {
        QString token = generateToken();
        m_sessions[token] = username;
        LoginWindow::currentUsername = username;
        resp["success"] = true;
        resp["token"] = token;
        resp["username"] = username;
        resp["is_admin"] = db.isAdmin(username);
    } else {
        resp["success"] = false;
        resp["error"] = "用户名或密码错误";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleRegister(QTcpSocket *socket, const QByteArray &body)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString username = input["username"].toString();
    QString password = input["password"].toString();

    QJsonObject resp;
    if (username.isEmpty() || password.isEmpty()) {
        resp["success"] = false;
        resp["error"] = "用户名和密码不能为空";
    } else if (db.registerUser(username, password)) {
        resp["success"] = true;
    } else {
        resp["success"] = false;
        resp["error"] = "用户名已存在";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleLogout(QTcpSocket *socket, const QString &token)
{
    m_sessions.remove(token);
    QJsonObject resp;
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleChangePassword(QTcpSocket *socket, const QByteArray &body, const QString &username)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString oldPassword = input["old_password"].toString();
    QString newPassword = input["new_password"].toString();

    QJsonObject resp;
    if (db.changePassword(username, oldPassword, newPassword)) {
        resp["success"] = true;
    } else {
        resp["success"] = false;
        resp["error"] = "旧密码错误";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleStatus(QTcpSocket *socket, const QString &username)
{
    QJsonObject resp;
    resp["user"] = username;
    resp["is_admin"] = db.isAdmin(username);

    if (mainWidget) {
        resp["weather"] = mainWidget->getWeatherDesc();
        resp["outdoor_temp"] = mainWidget->getOutdoorTemp();
        resp["indoor_temp"] = mainWidget->getIndoorTemp();
        resp["indoor_hum"] = mainWidget->getIndoorHum();
    }

    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDevices(QTcpSocket *socket, int userId)
{
    QJsonArray deviceArray;
    QList<Database::DeviceInfo> devices = db.getAllDevices(userId);
    for (const auto &d : devices) {
        QJsonObject obj;
        obj["name"] = d.name;
        obj["type"] = d.type;
        obj["room"] = d.room;
        obj["brand"] = d.brand;
        obj["state"] = db.getDeviceState(userId, d.room, d.name);
        deviceArray.append(obj);
    }
    QJsonObject resp;
    resp["devices"] = deviceArray;
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleTasks(QTcpSocket *socket, int userId)
{
    QJsonArray taskArray;
    QList<Database::TaskInfo> tasks = db.getTasks(userId);
    for (const auto &t : tasks) {
        QJsonObject obj;
        obj["id"] = t.id;
        obj["content"] = t.content;
        obj["completed"] = t.completed;
        obj["trigger_type"] = t.trigger_type;
        obj["trigger_value"] = t.trigger_value;
        obj["action_type"] = t.action_type;
        obj["action_target"] = t.action_target;
        obj["action_value"] = t.action_value;
        taskArray.append(obj);
    }
    QJsonObject resp;
    resp["tasks"] = taskArray;
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleRooms(QTcpSocket *socket, int userId)
{
    QJsonObject resp;
    resp["rooms"] = QJsonArray::fromStringList(db.getRooms(userId));
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleScenes(QTcpSocket *socket, int userId)
{
    QJsonObject resp;
    resp["scenes"] = QJsonArray::fromStringList(db.getScenes(userId));
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleApplyScene(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString sceneName = doc.object()["name"].toString();
    int sceneId = db.getSceneId(userId, sceneName);

    QJsonObject resp;
    if (sceneId != -1) {
        QList<Database::SceneDeviceInfo> sceneDevices = db.getSceneDevices(sceneId);
        for (const auto &sd : sceneDevices) {
            DeviceCommander::applyDeviceState(userId, sd.roomName, sd.deviceName, sd.deviceType, sd.state);
        }
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "情景不存在";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleControl(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString room = input["room"].toString();
    QString name = input["name"].toString();
    QString state = input["state"].toString();

    Database::DeviceInfo info = db.getDeviceInfo(userId, room, name);
    bool ok = DeviceCommander::applyDeviceState(userId, room, name, info.type, state);

    QJsonObject resp;
    if (ok) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "控制失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleCreateRoom(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString name = doc.object()["name"].toString();

    QJsonObject resp;
    if (name.isEmpty()) {
        resp["success"] = false;
        resp["error"] = "房间名称不能为空";
    } else if (db.createRoom(userId, name)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "房间已存在或创建失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDeleteRoom(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString name = doc.object()["name"].toString();

    QJsonObject resp;
    if (db.deleteRoom(userId, name)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "删除失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleCreateDevice(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString room = input["room"].toString();
    QString name = input["name"].toString();
    QString type = input["type"].toString();
    QString brand = input["brand"].toString();

    QJsonObject resp;
    if (room.isEmpty() || name.isEmpty() || type.isEmpty()) {
        resp["success"] = false;
        resp["error"] = "设备信息不完整";
    } else if (db.createDevice(userId, room, name, type, brand)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "设备已存在或创建失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDeleteDevice(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString room = input["room"].toString();
    QString name = input["name"].toString();
    QString type = input["type"].toString();

    QJsonObject resp;
    if (db.deleteDevice(userId, room, name, type)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "删除失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleCreateScene(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString name = doc.object()["name"].toString();

    QJsonObject resp;
    if (name.isEmpty()) {
        resp["success"] = false;
        resp["error"] = "情景名称不能为空";
    } else if (db.createScene(userId, name)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "情景已存在或创建失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDeleteScene(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString name = doc.object()["name"].toString();

    QJsonObject resp;
    if (db.deleteScene(userId, name)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "删除失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleSceneDevices(QTcpSocket *socket, const QString &sceneName, int userId)
{
    QJsonObject resp;
    int sceneId = db.getSceneId(userId, sceneName);
    if (sceneId == -1) {
        resp["success"] = false;
        resp["error"] = "情景不存在";
        sendJsonResponse(socket, resp);
        return;
    }

    QJsonArray deviceArray;
    QList<Database::SceneDeviceInfo> sceneDevices = db.getSceneDevices(sceneId);
    for (const auto &sd : sceneDevices) {
        QJsonObject obj;
        obj["device_name"] = sd.deviceName;
        obj["device_type"] = sd.deviceType;
        obj["room_name"] = sd.roomName;
        obj["state"] = sd.state;
        deviceArray.append(obj);
    }
    resp["devices"] = deviceArray;
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleSaveSceneDevices(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();
    QString sceneName = input["scene_name"].toString();
    QJsonArray devices = input["devices"].toArray();

    int sceneId = db.getSceneId(userId, sceneName);
    QJsonObject resp;
    if (sceneId == -1) {
        resp["success"] = false;
        resp["error"] = "情景不存在";
        sendJsonResponse(socket, resp);
        return;
    }

    bool success = true;
    for (const auto &devVal : devices) {
        QJsonObject dev = devVal.toObject();
        Database::SceneDeviceInfo info;
        info.deviceName = dev["device_name"].toString();
        info.deviceType = dev["device_type"].toString();
        info.roomName = dev["room_name"].toString();
        info.state = dev["state"].toString();
        if (!db.addDeviceToScene(sceneId, info)) {
            success = false;
            break;
        }
    }

    resp["success"] = success;
    if (!success) resp["error"] = "保存失败";
    sendJsonResponse(socket, resp);
}

void HttpServer::handleCreateTask(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();

    Database::TaskInfo info;
    info.trigger_type = input["trigger_type"].toString();
    info.trigger_value = input["trigger_value"].toString();
    info.action_type = input["action_type"].toString();
    info.action_target = input["action_target"].toString();
    info.action_value = input["action_value"].toString();
    info.content = QString("当[%1 %2]时 -> %3[%4] %5")
        .arg(info.trigger_type, info.trigger_value, info.action_type, info.action_target, info.action_value);

    QJsonObject resp;
    if (info.trigger_value.isEmpty() || info.action_target.isEmpty()) {
        resp["success"] = false;
        resp["error"] = "请填写完整的触发条件和目标对象";
    } else if (db.createTask(userId, info)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "创建失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleUpdateTask(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject input = doc.object();

    Database::TaskInfo info;
    info.id = input["id"].toInt();
    info.trigger_type = input["trigger_type"].toString();
    info.trigger_value = input["trigger_value"].toString();
    info.action_type = input["action_type"].toString();
    info.action_target = input["action_target"].toString();
    info.action_value = input["action_value"].toString();
    info.content = QString("当[%1 %2]时 -> %3[%4] %5")
        .arg(info.trigger_type, info.trigger_value, info.action_type, info.action_target, info.action_value);

    QJsonObject resp;
    if (db.updateTask(info)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "更新失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDeleteTask(QTcpSocket *socket, const QByteArray &body, int userId)
{
    QJsonDocument doc = QJsonDocument::fromJson(body);
    int taskId = doc.object()["id"].toInt();

    QJsonObject resp;
    if (db.deleteTask(taskId)) {
        resp["success"] = true;
        emit dataChanged();
    } else {
        resp["success"] = false;
        resp["error"] = "删除失败";
    }
    sendJsonResponse(socket, resp);
}

void HttpServer::handleUsers(QTcpSocket *socket, const QString &username)
{
    QJsonObject resp;
    if (!db.isAdmin(username)) {
        resp["success"] = false;
        resp["error"] = "权限不足";
        sendJsonResponse(socket, resp);
        return;
    }

    QJsonArray userArray;
    QStringList users = db.getAllUsers();
    for (const auto &u : users) {
        QJsonObject obj;
        obj["username"] = u;
        obj["is_admin"] = db.isAdmin(u);
        userArray.append(obj);
    }
    resp["users"] = userArray;
    resp["success"] = true;
    sendJsonResponse(socket, resp);
}

void HttpServer::handleDeleteUser(QTcpSocket *socket, const QByteArray &body, const QString &username)
{
    QJsonObject resp;
    if (!db.isAdmin(username)) {
        resp["success"] = false;
        resp["error"] = "权限不足";
        sendJsonResponse(socket, resp);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    QString targetUser = doc.object()["username"].toString();

    if (targetUser == username) {
        resp["success"] = false;
        resp["error"] = "不能删除自己";
    } else if (db.deleteUser(targetUser)) {
        resp["success"] = true;
    } else {
        resp["success"] = false;
        resp["error"] = "删除失败";
    }
    sendJsonResponse(socket, resp);
}
