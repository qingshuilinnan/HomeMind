#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>

class Database : public QObject
{
    Q_OBJECT

public:
    explicit Database(QObject *parent = nullptr);
    ~Database();

    static QString hashPassword(const QString &password);
    static bool verifyPassword(const QString &password, const QString &hashedPassword);
    
    bool open();
    void close();
    bool isOpen() const;
    
    // 用户相关操作
    int getUserId(const QString &username);
    
    // 房间相关操作
    bool createRoom(int userId, const QString &name);
    bool deleteRoom(int userId, const QString &name);
    QStringList getRooms(int userId);
    
    // 设备相关操作
    bool createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand = "");
    bool deleteDevice(int userId, const QString &room, const QString &name, const QString &type);
    bool updateDeviceState(int userId, const QString &room, const QString &name, const QString &state);
    QString getDeviceState(int userId, const QString &room, const QString &name);
    QStringList getDevicesByRoomAndType(int userId, const QString &room, const QString &type);
    bool isDeviceNameExists(int userId, const QString &room, const QString &name, const QString &type);
    struct DeviceInfo {
        QString name;
        QString type;
        QString room;
        QString brand;
        QString ipAddress;
        int tcpPort = 0;
        QString deviceId;
    };
    QList<DeviceInfo> getAllDevices(int userId);
    bool createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand, const QString &ipAddress, int tcpPort);
    bool createDevice(int userId, const QString &room, const QString &name, const QString &type, const QString &brand, const QString &ipAddress, int tcpPort, const QString &deviceId);
    DeviceInfo getDeviceInfo(int userId, const QString &room, const QString &name);
    
    // 情景相关操作
    bool createScene(int userId, const QString &name);
    bool deleteScene(int userId, const QString &name);
    bool updateScene(int userId, const QString &oldName, const QString &newName);
    QStringList getScenes(int userId);
    int getSceneId(int userId, const QString &sceneName);
    
    // 情景设备关联操作
    struct SceneDeviceInfo {
        QString deviceName;
        QString deviceType;
        QString roomName;
        QString state;
    };
    bool addDeviceToScene(int sceneId, const SceneDeviceInfo &info);
    bool removeDeviceFromScene(int sceneId, const QString &deviceName, const QString &roomName);
    QList<SceneDeviceInfo> getSceneDevices(int sceneId);
    
    // 用户相关操作
    bool registerUser(const QString &username, const QString &password, bool isAdmin = false);
    bool loginUser(const QString &username, const QString &password);
    bool changePassword(const QString &username, const QString &oldPassword, const QString &newPassword);
    bool resetPassword(const QString &username, const QString &newPassword);
    bool isAdmin(const QString &username);
    QStringList getAllUsers();
    bool deleteUser(const QString &username);

    // 位置信息持久化
    bool saveUserLocation(const QString &username, const QString &location);
    QString getUserLocation(const QString &username);

    // 自动登录相关
    bool setAutoLogin(const QString &username, bool enable);
    QString getAutoLoginUser();

    // 任务相关操作
    struct TaskInfo {
        int id;
        QString content; // 任务描述摘要
        bool completed;
        QString trigger_type;   // 时间, 室内温度, 室外温度
        QString trigger_value;  // 具体触发值 (如 "08:00", ">28")
        QString action_type;    // 应用场景, 控制设备
        QString action_target;  // 场景名称 或 设备名称
        QString action_value;   // 具体动作 (如 "打开", "26°C")
    };
    bool createTask(int userId, const TaskInfo &info);
    bool updateTask(const TaskInfo &info);
    bool deleteTask(int taskId);
    bool updateTaskStatus(int taskId, bool completed);
    QList<TaskInfo> getTasks(int userId);

private:
    QSqlDatabase db;
    bool createTables();
};

#endif // DATABASE_H