#include "database.h"
#include <QDebug>
#include <QSet>
#include <QCryptographicHash>

Database::Database(QObject *parent)
    : QObject(parent)
{
}

QString Database::hashPassword(const QString &password)
{
    QByteArray salt = "HomeMind_Salt_2024";
    QString saltedPassword = password + salt;
    QByteArray hash = QCryptographicHash::hash(saltedPassword.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

bool Database::verifyPassword(const QString &password, const QString &hashedPassword)
{
    return hashPassword(password) == hashedPassword;
}

Database::~Database()
{
    // 不在析构函数中自动关闭连接，
    // 因为多个 Database 实例可能共享同一个默认连接。
}

bool Database::open()
{
    if (QSqlDatabase::contains(QSqlDatabase::defaultConnection)) {
        db = QSqlDatabase::database(QSqlDatabase::defaultConnection);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName("HomeMind.db");
    }
    
    if (!db.isOpen()) {
        if (!db.open()) {
            qDebug() << "Failed to open database:" << db.lastError().text();
            return false;
        }
    }
    
    return createTables();
}

void Database::close()
{
    // 显式关闭连接
    if (db.isOpen()) {
        db.close();
    }
}

bool Database::isOpen() const
{
    return db.isOpen();
}

bool Database::createTables()
{
    QSqlQuery query;
    
    // 创建所有表
    if (!query.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE, password TEXT, is_admin INTEGER DEFAULT 0)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS rooms (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, name TEXT, UNIQUE(user_id, name), FOREIGN KEY(user_id) REFERENCES users(id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS devices (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, name TEXT, type TEXT, room TEXT, brand TEXT, state TEXT, FOREIGN KEY(user_id) REFERENCES users(id), FOREIGN KEY(room) REFERENCES rooms(name))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS scenes (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, name TEXT, UNIQUE(user_id, name), FOREIGN KEY(user_id) REFERENCES users(id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS scene_devices (id INTEGER PRIMARY KEY AUTOINCREMENT, scene_id INTEGER, device_name TEXT, device_type TEXT, room_name TEXT, state TEXT, UNIQUE(scene_id, device_name, room_name), FOREIGN KEY(scene_id) REFERENCES scenes(id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS tasks (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, content TEXT, completed INTEGER DEFAULT 0, trigger_type TEXT, trigger_value TEXT, action_type TEXT, action_target TEXT, action_value TEXT, FOREIGN KEY(user_id) REFERENCES users(id))")
        ) {
        qDebug() << "Failed to create tables:" << query.lastError().text();
        return false;
    }

    // 数据库迁移与表结构修正
    // 1. 检查 devices 表列
    query.exec("PRAGMA table_info(devices)");
    QSet<QString> deviceColumns;
    while (query.next()) {
        deviceColumns.insert(query.value(1).toString());
    }
    if (!deviceColumns.contains("brand")) query.exec("ALTER TABLE devices ADD COLUMN brand TEXT");
    if (!deviceColumns.contains("state")) query.exec("ALTER TABLE devices ADD COLUMN state TEXT");
    if (!deviceColumns.contains("ip_address")) query.exec("ALTER TABLE devices ADD COLUMN ip_address TEXT DEFAULT ''");
    if (!deviceColumns.contains("tcp_port")) query.exec("ALTER TABLE devices ADD COLUMN tcp_port INTEGER DEFAULT 0");
    if (!deviceColumns.contains("device_id")) query.exec("ALTER TABLE devices ADD COLUMN device_id TEXT DEFAULT ''");

    // 2. 检查 users 表列，添加 location 字段
    query.exec("PRAGMA table_info(users)");
    QSet<QString> userColumns;
    while (query.next()) {
        userColumns.insert(query.value(1).toString());
    }
    if (!userColumns.contains("location")) {
        query.exec("ALTER TABLE users ADD COLUMN location TEXT");
    }
    if (!userColumns.contains("keep_login")) {
        query.exec("ALTER TABLE users ADD COLUMN keep_login INTEGER DEFAULT 0");
    }

    // 3. 检查并修正 scene_devices 的唯一约束 (SQLite 不支持直接 ALTER TABLE ADD UNIQUE)
    // 如果没有唯一约束，则重建该表以支持“应用情景”时的精准覆盖
    query.exec("PRAGMA index_list(scene_devices)");
    bool hasUniqueIndex = false;
    while (query.next()) {
        if (query.value(2).toInt() == 1) { // 1 表示唯一索引
            hasUniqueIndex = true;
            break;
        }
    }
    
    if (!hasUniqueIndex) {
        // 备份数据，重建表，恢复数据（或直接重建清空旧冲突数据）
        query.exec("DROP TABLE IF EXISTS scene_devices");
        query.exec("CREATE TABLE scene_devices (id INTEGER PRIMARY KEY AUTOINCREMENT, scene_id INTEGER, device_name TEXT, device_type TEXT, room_name TEXT, state TEXT, UNIQUE(scene_id, device_name, room_name), FOREIGN KEY(scene_id) REFERENCES scenes(id))");
    }

    // 4. 检查 tasks 表列，添加自动化所需字段
    query.exec("PRAGMA table_info(tasks)");
    QSet<QString> taskColumns;
    while (query.next()) {
        taskColumns.insert(query.value(1).toString());
    }
    if (!taskColumns.contains("trigger_type")) query.exec("ALTER TABLE tasks ADD COLUMN trigger_type TEXT");
    if (!taskColumns.contains("trigger_value")) query.exec("ALTER TABLE tasks ADD COLUMN trigger_value TEXT");
    if (!taskColumns.contains("action_type")) query.exec("ALTER TABLE tasks ADD COLUMN action_type TEXT");
    if (!taskColumns.contains("action_target")) query.exec("ALTER TABLE tasks ADD COLUMN action_target TEXT");
    if (!taskColumns.contains("action_value")) query.exec("ALTER TABLE tasks ADD COLUMN action_value TEXT");
    // 移除旧的 deadline 字段 (SQLite 不支持 DROP COLUMN，这里保留它也没关系)

    // 强制检查并创建 admin 用户 (密码哈希存储)
    query.prepare("INSERT OR IGNORE INTO users (username, password, is_admin) VALUES ('admin', ?, 1)");
    query.addBindValue(hashPassword("admin"));
    query.exec();
    
    return true;
}

bool Database::saveUserLocation(const QString &username, const QString &location)
{
    QSqlQuery query;
    query.prepare("UPDATE users SET location = ? WHERE username = ?");
    query.addBindValue(location);
    query.addBindValue(username);
    return query.exec();
}

QString Database::getUserLocation(const QString &username)
{
    QSqlQuery query;
    query.prepare("SELECT location FROM users WHERE username = ?");
    query.addBindValue(username);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

bool Database::setAutoLogin(const QString &username, bool enable)
{
    QSqlQuery query;
    // 首先将所有用户的 keep_login 置为 0 (只允许一个用户保持登录)
    query.exec("UPDATE users SET keep_login = 0");
    
    if (enable) {
        query.prepare("UPDATE users SET keep_login = 1 WHERE username = ?");
        query.addBindValue(username);
        return query.exec();
    }
    return true;
}

QString Database::getAutoLoginUser()
{
    QSqlQuery query;
    query.exec("SELECT username FROM users WHERE keep_login = 1");
    if (query.next()) {
        return query.value(0).toString();
    }
    return "";
}

bool Database::createTask(int userId, const TaskInfo &info)
{
    QSqlQuery query;
    query.prepare("INSERT INTO tasks (user_id, content, trigger_type, trigger_value, action_type, action_target, action_value) VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(userId);
    query.addBindValue(info.content);
    query.addBindValue(info.trigger_type);
    query.addBindValue(info.trigger_value);
    query.addBindValue(info.action_type);
    query.addBindValue(info.action_target);
    query.addBindValue(info.action_value);
    return query.exec();
}

bool Database::updateTask(const TaskInfo &info)
{
    QSqlQuery query;
    query.prepare("UPDATE tasks SET content = ?, trigger_type = ?, trigger_value = ?, action_type = ?, action_target = ?, action_value = ?, completed = 0 WHERE id = ?");
    query.addBindValue(info.content);
    query.addBindValue(info.trigger_type);
    query.addBindValue(info.trigger_value);
    query.addBindValue(info.action_type);
    query.addBindValue(info.action_target);
    query.addBindValue(info.action_value);
    query.addBindValue(info.id);
    return query.exec();
}

bool Database::deleteTask(int taskId)
{
    QSqlQuery query;
    query.prepare("DELETE FROM tasks WHERE id = ?");
    query.addBindValue(taskId);
    return query.exec();
}

bool Database::updateTaskStatus(int taskId, bool completed)
{
    QSqlQuery query;
    query.prepare("UPDATE tasks SET completed = ? WHERE id = ?");
    query.addBindValue(completed ? 1 : 0);
    query.addBindValue(taskId);
    return query.exec();
}

QList<Database::TaskInfo> Database::getTasks(int userId)
{
    QList<TaskInfo> tasks;
    QSqlQuery query;
    query.prepare("SELECT id, content, completed, trigger_type, trigger_value, action_type, action_target, action_value FROM tasks WHERE user_id = ? ORDER BY id DESC");
    query.addBindValue(userId);
    
    if (query.exec()) {
        while (query.next()) {
            TaskInfo info;
            info.id = query.value(0).toInt();
            info.content = query.value(1).toString();
            info.completed = query.value(2).toInt() == 1;
            info.trigger_type = query.value(3).toString();
            info.trigger_value = query.value(4).toString();
            info.action_type = query.value(5).toString();
            info.action_target = query.value(6).toString();
            info.action_value = query.value(7).toString();
            tasks.append(info);
        }
    }
    return tasks;
}

int Database::getUserId(const QString &username)
{
    QSqlQuery query;
    query.prepare("SELECT id FROM users WHERE username = ?");
    query.addBindValue(username);
    
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    
    return -1;
}

bool Database::createRoom(int userId, const QString &name)
{
    QSqlQuery query;
    query.prepare("INSERT INTO rooms (user_id, name) VALUES (?, ?)");
    query.addBindValue(userId);
    query.addBindValue(name);
    
    if (!query.exec()) {
        qDebug() << "Failed to create room:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool Database::deleteRoom(int userId, const QString &name)
{
    QSqlQuery query;
    
    // 先删除房间内的设备
    query.prepare("DELETE FROM devices WHERE user_id = ? AND room = ?");
    query.addBindValue(userId);
    query.addBindValue(name);
    if (!query.exec()) {
        qDebug() << "Failed to delete devices:" << query.lastError().text();
        return false;
    }
    
    // 再删除房间
    query.prepare("DELETE FROM rooms WHERE user_id = ? AND name = ?");
    query.addBindValue(userId);
    query.addBindValue(name);
    if (!query.exec()) {
        qDebug() << "Failed to delete room:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QStringList Database::getRooms(int userId)
{
    QStringList rooms;
    QSqlQuery query;
    query.prepare("SELECT name FROM rooms WHERE user_id = ?");
    query.addBindValue(userId);
    
    if (query.exec()) {
        while (query.next()) {
            rooms << query.value(0).toString();
        }
    }
    
    return rooms;
}

bool Database::createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand)
{
    QSqlQuery query;
    query.prepare("INSERT INTO devices (user_id, name, type, room, brand) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(userId);
    query.addBindValue(name);
    query.addBindValue(type);
    query.addBindValue(room);
    query.addBindValue(brand);
    
    if (!query.exec()) {
        qDebug() << "Failed to create device:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool Database::createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand, const QString &ipAddress, int tcpPort)
{
    QSqlQuery query;
    query.prepare("INSERT INTO devices (user_id, name, type, room, brand, ip_address, tcp_port) VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(userId);
    query.addBindValue(name);
    query.addBindValue(type);
    query.addBindValue(room);
    query.addBindValue(brand);
    query.addBindValue(ipAddress);
    query.addBindValue(tcpPort);

    if (!query.exec()) {
        qDebug() << "Failed to create device:" << query.lastError().text();
        return false;
    }

    return true;
}

bool Database::createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand, const QString &ipAddress, int tcpPort, const QString &deviceId)
{
    QSqlQuery query;
    query.prepare("INSERT INTO devices (user_id, name, type, room, brand, ip_address, tcp_port, device_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(userId);
    query.addBindValue(name);
    query.addBindValue(type);
    query.addBindValue(room);
    query.addBindValue(brand);
    query.addBindValue(ipAddress);
    query.addBindValue(tcpPort);
    query.addBindValue(deviceId);

    if (!query.exec()) {
        qDebug() << "Failed to create device:" << query.lastError().text();
        return false;
    }

    return true;
}

Database::DeviceInfo Database::getDeviceInfo(int userId, const QString &room, const QString &name)
{
    DeviceInfo info;
    QSqlQuery query;
    query.prepare("SELECT name, type, room, brand, ip_address, tcp_port, device_id FROM devices WHERE user_id = ? AND room = ? AND name = ?");
    query.addBindValue(userId);
    query.addBindValue(room);
    query.addBindValue(name);
    if (query.exec() && query.next()) {
        info.name = query.value(0).toString();
        info.type = query.value(1).toString();
        info.room = query.value(2).toString();
        info.brand = query.value(3).toString();
        info.ipAddress = query.value(4).toString();
        info.tcpPort = query.value(5).toInt();
        info.deviceId = query.value(6).toString();
    }
    return info;
}

bool Database::deleteDevice(int userId, const QString &room, const QString &name, const QString &type)
{
    QSqlQuery query;
    query.prepare("DELETE FROM devices WHERE user_id = ? AND name = ? AND room = ? AND type = ?");
    query.addBindValue(userId);
    query.addBindValue(name);
    query.addBindValue(room);
    query.addBindValue(type);
    
    if (!query.exec()) {
        qDebug() << "Failed to delete device:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool Database::updateDeviceState(int userId, const QString &room, const QString &name, const QString &state)
{
    QSqlQuery query;
    query.prepare("UPDATE devices SET state = ? WHERE user_id = ? AND room = ? AND name = ?");
    query.addBindValue(state);
    query.addBindValue(userId);
    query.addBindValue(room);
    query.addBindValue(name);
    return query.exec();
}

QString Database::getDeviceState(int userId, const QString &room, const QString &name)
{
    QSqlQuery query;
    query.prepare("SELECT state FROM devices WHERE user_id = ? AND room = ? AND name = ?");
    query.addBindValue(userId);
    query.addBindValue(room);
    query.addBindValue(name);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

QStringList Database::getDevicesByRoomAndType(int userId, const QString &room, const QString &type)
{
    QStringList devices;
    QSqlQuery query;
    
    query.prepare("SELECT name FROM devices WHERE user_id = ? AND room = ? AND type = ?");
    query.addBindValue(userId);
    query.addBindValue(room);
    query.addBindValue(type);
    
    if (query.exec()) {
        while (query.next()) {
            devices << query.value(0).toString();
        }
    } else {
        qDebug() << "Failed to get devices:" << query.lastError().text();
    }
    
    return devices;
}

bool Database::isDeviceNameExists(int userId, const QString &room, const QString &name, const QString &type)
{
    QSqlQuery query;
    query.prepare("SELECT * FROM devices WHERE user_id = ? AND name = ? AND room = ? AND type = ?");
    query.addBindValue(userId);
    query.addBindValue(name);
    query.addBindValue(room);
    query.addBindValue(type);
    
    if (!query.exec()) {
        qDebug() << "Failed to check device name:" << query.lastError().text();
        return false;
    }
    
    return query.next();
}

QList<Database::DeviceInfo> Database::getAllDevices(int userId)
{
    QList<DeviceInfo> devices;
    QSqlQuery query;
    query.prepare("SELECT name, type, room, brand, ip_address, tcp_port, device_id FROM devices WHERE user_id = ?");
    query.addBindValue(userId);
    
    if (query.exec()) {
        while (query.next()) {
            DeviceInfo info;
            info.name = query.value(0).toString();
            info.type = query.value(1).toString();
            info.room = query.value(2).toString();
            info.brand = query.value(3).toString();
            info.ipAddress = query.value(4).toString();
            info.tcpPort = query.value(5).toInt();
            info.deviceId = query.value(6).toString();
            devices.append(info);
        }
    }
    return devices;
}

bool Database::createScene(int userId, const QString &name)
{
    QSqlQuery query;
    query.prepare("INSERT INTO scenes (user_id, name) VALUES (?, ?)");
    query.addBindValue(userId);
    query.addBindValue(name);
    
    if (!query.exec()) {
        qDebug() << "Failed to create scene:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool Database::deleteScene(int userId, const QString &name)
{
    QSqlQuery query;
    query.prepare("DELETE FROM scenes WHERE user_id = ? AND name = ?");
    query.addBindValue(userId);
    query.addBindValue(name);
    
    if (!query.exec()) {
        qDebug() << "Failed to delete scene:" << query.lastError().text();
        return false;
    }
    
    return true;
}

bool Database::updateScene(int userId, const QString &oldName, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE scenes SET name = ? WHERE user_id = ? AND name = ?");
    query.addBindValue(newName);
    query.addBindValue(userId);
    query.addBindValue(oldName);
    
    if (!query.exec()) {
        qDebug() << "Failed to update scene:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QStringList Database::getScenes(int userId)
{
    QStringList scenes;
    QSqlQuery query;
    query.prepare("SELECT name FROM scenes WHERE user_id = ?");
    query.addBindValue(userId);
    
    if (query.exec()) {
        while (query.next()) {
            scenes << query.value(0).toString();
        }
    }
    
    return scenes;
}

int Database::getSceneId(int userId, const QString &sceneName)
{
    QSqlQuery query;
    query.prepare("SELECT id FROM scenes WHERE user_id = ? AND name = ?");
    query.addBindValue(userId);
    query.addBindValue(sceneName);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

bool Database::addDeviceToScene(int sceneId, const SceneDeviceInfo &info)
{
    QSqlQuery query;
    // 先检查是否存在，存在则更新，不存在则插入
    query.prepare("INSERT OR REPLACE INTO scene_devices (scene_id, device_name, device_type, room_name, state) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(sceneId);
    query.addBindValue(info.deviceName);
    query.addBindValue(info.deviceType);
    query.addBindValue(info.roomName);
    query.addBindValue(info.state);
    return query.exec();
}

bool Database::removeDeviceFromScene(int sceneId, const QString &deviceName, const QString &roomName)
{
    QSqlQuery query;
    query.prepare("DELETE FROM scene_devices WHERE scene_id = ? AND device_name = ? AND room_name = ?");
    query.addBindValue(sceneId);
    query.addBindValue(deviceName);
    query.addBindValue(roomName);
    return query.exec();
}

QList<Database::SceneDeviceInfo> Database::getSceneDevices(int sceneId)
{
    QList<SceneDeviceInfo> devices;
    QSqlQuery query;
    query.prepare("SELECT device_name, device_type, room_name, state FROM scene_devices WHERE scene_id = ?");
    query.addBindValue(sceneId);
    if (query.exec()) {
        while (query.next()) {
            SceneDeviceInfo info;
            info.deviceName = query.value(0).toString();
            info.deviceType = query.value(1).toString();
            info.roomName = query.value(2).toString();
            info.state = query.value(3).toString();
            devices.append(info);
        }
    }
    return devices;
}

bool Database::registerUser(const QString &username, const QString &password, bool isAdmin)
{
    QSqlQuery query;
    query.prepare("INSERT INTO users (username, password, is_admin) VALUES (?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(hashPassword(password));
    query.addBindValue(isAdmin ? 1 : 0);

    if (!query.exec()) {
        qDebug() << "Failed to register user:" << query.lastError().text();
        return false;
    }

    return true;
}

bool Database::loginUser(const QString &username, const QString &password)
{
    QSqlQuery query;
    query.prepare("SELECT password FROM users WHERE username = ?");
    query.addBindValue(username);

    if (!query.exec()) {
        qDebug() << "Failed to login user:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        QString storedHash = query.value(0).toString();
        if (storedHash.isEmpty()) {
            return false;
        }
        return verifyPassword(password, storedHash);
    }

    return false;
}

bool Database::changePassword(const QString &username, const QString &oldPassword, const QString &newPassword)
{
    // Verify old password first
    if (!loginUser(username, oldPassword)) {
        return false;
    }
    return resetPassword(username, newPassword);
}

bool Database::resetPassword(const QString &username, const QString &newPassword)
{
    QSqlQuery query;
    query.prepare("UPDATE users SET password = ? WHERE username = ?");
    query.addBindValue(hashPassword(newPassword));
    query.addBindValue(username);

    if (!query.exec()) {
        qDebug() << "Failed to reset password:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool Database::isAdmin(const QString &username)
{
    QSqlQuery query;
    query.prepare("SELECT is_admin FROM users WHERE username = ?");
    query.addBindValue(username);
    
    if (!query.exec() || !query.next()) {
        return false;
    }
    
    return query.value(0).toInt() == 1;
}

QStringList Database::getAllUsers()
{
    QStringList users;
    QSqlQuery query("SELECT username FROM users");
    
    while (query.next()) {
        users << query.value(0).toString();
    }
    
    return users;
}

bool Database::deleteUser(const QString &username)
{
    QSqlQuery query;
    
    // 先获取用户ID
    query.prepare("SELECT id FROM users WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec() || !query.next()) {
        return false;
    }
    
    int userId = query.value(0).toInt();
    
    // 先删除用户的设备
    query.prepare("DELETE FROM devices WHERE user_id = ?");
    query.addBindValue(userId);
    if (!query.exec()) {
        qDebug() << "Failed to delete user devices:" << query.lastError().text();
        return false;
    }
    
    // 再删除用户的房间
    query.prepare("DELETE FROM rooms WHERE user_id = ?");
    query.addBindValue(userId);
    if (!query.exec()) {
        qDebug() << "Failed to delete user rooms:" << query.lastError().text();
        return false;
    }
    
    // 再删除用户的情景
    query.prepare("DELETE FROM scenes WHERE user_id = ?");
    query.addBindValue(userId);
    if (!query.exec()) {
        qDebug() << "Failed to delete user scenes:" << query.lastError().text();
        return false;
    }
    
    // 最后删除用户
    query.prepare("DELETE FROM users WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec()) {
        qDebug() << "Failed to delete user:" << query.lastError().text();
        return false;
    }
    
    return true;
}