# HomeMind 插件开发指南

本文档详细介绍 HomeMind 插件系统的实现原理、插件管理机制以及如何开发新的设备插件。

## 目录

1. [架构概述](#架构概述)
2. [插件接口详解](#插件接口详解)
3. [HostServices 宿主服务](#hostservices-宿主服务)
4. [PluginManager 插件管理器](#pluginmanager-插件管理器)
5. [插件生命周期与管理流程](#插件生命周期与管理流程)
6. [从零开始开发一个插件](#从零开始开发一个插件)
7. [现有插件分析](#现有插件分析)
8. [构建与调试](#构建与调试)
9. [注意事项](#注意事项)

---

## 架构概述

HomeMind 使用 Qt 插件机制实现设备类型扩展。每个设备类型（空调、灯光、传感器等）对应一个独立的共享库（`.so` / `.dll`），在运行时由 `PluginManager` 动态加载。

```
┌──────────────────────────────────────────────────────────────┐
│                       HomeMind 主程序                         │
│                                                              │
│  ┌────────────────┐     ┌───────────────────────────────┐    │
│  │  PluginManager  │────▶│   DevicePluginInterface        │    │
│  │  (单例/加载/管理) │     │   (抽象接口 Q_DECLARE_INTERFACE)│    │
│  └────────────────┘     └───────────────────────────────┘    │
│         │                            ▲                        │
│         │ QPluginLoader              │ 实现                    │
│         │ 加载 .so                   │                        │
│         ▼                            │                        │
│  ┌────────────┐  ┌─────────────┐  ┌──────────────┐          │
│  │ acplugin   │  │ lightplugin  │  │ sensorplugin  │   ...    │
│  │  空调插件    │  │  灯光插件     │  │  传感器插件    │          │
│  └────────────┘  └─────────────┘  └──────────────┘          │
│                                                              │
│  ┌───────────────────────────────────────────────────────┐   │
│  │              主程序 UI（插件管理 Tab）                    │   │
│  │  [编译插件]  [重新加载插件]  [移除插件]                    │   │
│  └───────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 核心组件

| 组件 | 文件 | 职责 |
|---|---|---|
| `DevicePluginInterface` | `plugin/deviceplugininterface.h` | 插件抽象接口，定义所有插件必须实现的方法 |
| `PluginManager` | `plugin/pluginmanager.h/.cpp` | 单例，负责从指定目录加载 `.so` 文件并注册插件 |
| `HostServices` | `plugin/deviceplugininterface.h` | 宿主服务结构体，向插件提供数据库、设备发现、命令发送等能力 |

### 目录结构

```
HomeMind/
├── plugin/                          # 插件框架（接口 + 管理器）
│   ├── deviceplugininterface.h      # 插件抽象接口
│   ├── pluginmanager.h / .cpp       # 插件管理器
│   └── ...
├── plugins/                         # 插件源码目录
│   ├── acplugin/                    # 空调插件
│   │   ├── acplugin.pro
│   │   ├── acplugin.h / .cpp
│   │   ├── acplugin.json
│   │   ├── accontrol.h / .cpp / .ui
│   ├── lightplugin/                 # 灯光插件
│   │   ├── lightplugin.pro
│   │   ├── lightplugin.h / .cpp
│   │   ├── lightplugin.json
│   │   ├── lightcontrol.h / .cpp / .ui
│   └── sensorplugin/                # 传感器插件
│       ├── sensorplugin.pro
│       ├── sensorplugin.h / .cpp
│       ├── sensorplugin.json
│       ├── sensorcontrol.h / .cpp
└── build/
    └── plugins/                     # 编译输出目录
        ├── acplugin/
        │   └── libacplugin.so
        ├── lightplugin/
        │   └── liblightplugin.so
        └── sensorplugin/
            └── libsensorplugin.so
```

---

## 插件接口详解

`DevicePluginInterface` 定义在 `plugin/deviceplugininterface.h` 中。

### 元数据方法（纯虚函数，必须实现）

```cpp
// 设备类型标识，用于数据库存储和插件匹配
// 例："空调"、"灯光"、"传感器"
// 数据库中 devices.type 字段存储的就是这个值
virtual QString deviceType() const = 0;

// 显示名称，用于 Tab 页标题
// 例："空调控制"
virtual QString displayName() const = 0;

// 设备描述，用于说明文字
virtual QString description() const = 0;

// 图标文本（Emoji 或 Unicode 字符），用于 Tab 页图标
// 例："☁️"、"💡"、"🌡"
virtual QString iconText() const = 0;

// MQTT 设备类型标识，用于设备发现时匹配
// 例："ac_device"、"light_device"、"sensor_device"
// 这个值需要与 ESP 固件中上报的 deviceType 一致
virtual QString mqttDeviceType() const = 0;
```

### 生命周期方法（纯虚函数，必须实现）

```cpp
// 插件初始化/销毁
// - 首次加载时传入 HostServices 指针，插件应保存此指针
// - 应用退出或插件卸载时传入 nullptr，插件应清理资源
virtual void initialize(HostServices *services) = 0;
```

### 控件工厂方法（纯虚函数，必须实现）

```cpp
// 创建设备控制界面
// 当用户在房间详情中点击某个设备时调用
// 参数：
//   roomName   - 设备所在房间名
//   deviceName - 设备名称
//   parent     - 父控件（可为 nullptr，此时控件作为独立窗口）
// 返回值：一个 QWidget*，主程序会调用 show() 显示它
virtual QWidget *createControlWidget(const QString &roomName,
                                     const QString &deviceName,
                                     QWidget *parent = nullptr) = 0;
```

### 情景模式方法（纯虚函数，必须实现）

```cpp
// 创建情景状态编辑器
// 在情景配置对话框中，为该设备类型创建一个状态编辑控件
// 例：灯光插件返回一个"开启/关闭"下拉框
//     空调插件返回一个"开关+温度"组合控件
// 如果设备不参与情景（如传感器），返回 nullptr
virtual QWidget *createSceneStateEditor(QWidget *parent) = 0;

// 从情景编辑器中提取结果
// 读取 createSceneStateEditor 创建的控件当前值，返回状态字符串
// 例：灯光返回 "开启"
//     空调返回 "开启, 26°C"
virtual QString sceneEditorResult(QWidget *editor) const = 0;
```

### 命令构建方法（纯虚函数，必须实现）

```cpp
// 根据状态字符串构建 MQTT 命令
// 情景执行时，主程序调用此方法将状态字符串转换为 MQTT 命令序列
// 返回值：QJsonObject 列表，每个对象代表一条命令
// 命令格式由插件自行定义，通常包含 "type" 和 "value" 字段
// 只读设备（如传感器）返回空列表
virtual QList<QJsonObject> buildCommands(const QString &state) const = 0;
```

### 状态显示方法（纯虚函数，必须实现）

```cpp
// 将状态字符串转换为显示文本
// 用于情景列表中展示设备状态
// 例："开启" → "运行中"
//     "关闭" → "已关机"
virtual QString stateDisplayText(const QString &state) const = 0;
```

### 设备添加方法（可选，有默认实现）

```cpp
// 是否支持自动扫描添加
// 默认返回 false，重写返回 true 后，添加设备对话框会显示"扫描"按钮
virtual bool supportsScan() const { return false; }

// 显示添加设备对话框（扫描 + 手动添加）
// 点击房间详情中的"+ 添加XX设备"时调用
// 返回 true 表示成功添加了设备
virtual bool showAddDeviceDialog(QWidget *parent, const QString &roomName,
                                 const QString &username) { return false; }

// 显示手动添加对话框（仅手动添加）
// 通常不需要重写，在 showAddDeviceDialog 中自行处理即可
virtual bool showManualAddDialog(QWidget *parent, const QString &roomName,
                                 const QString &username) { return false; }
```

---

## HostServices 宿主服务

`HostServices` 是主程序通过依赖注入提供给插件的服务接口：

```cpp
struct HostServices {
    Database *db;                        // 数据库访问指针
    DeviceDiscovery *discovery;          // MQTT 设备发现服务
    QString currentUsername;             // 当前登录用户名

    // 发送 MQTT 命令到设备
    // 签名：bool (*)(const QString &deviceId, const QJsonObject &cmd)
    SendCommandFunc sendCommand;

    // 异步获取传感器数据
    // 签名：bool (*)(const QString &deviceId,
    //                double &temperature, double &humidity,
    //                int &light, bool &motion)
    FetchSensorDataAsyncFunc fetchSensorDataAsync;

    // 缓存传感器数据
    // 签名：void (*)(const QString &deviceId, const QJsonObject &data)
    CacheSensorDataFunc cacheSensorData;
};
```

### 使用示例

```cpp
// 在 initialize 中保存服务指针
void MyPlugin::initialize(HostServices *services)
{
    m_services = services;
}

// 在控制控件中发送命令
void MyControl::sendCommand(const QJsonObject &cmd)
{
    if (m_services && m_services->sendCommand) {
        m_services->sendCommand(m_deviceId, cmd);
    }
}

// 获取传感器数据
void MyControl::fetchSensorData()
{
    if (m_services && m_services->fetchSensorDataAsync) {
        double temp, hum;
        int light;
        bool motion;
        m_services->fetchSensorDataAsync(m_deviceId, temp, hum, light, motion);
    }
}
```

---

## PluginManager 插件管理器

`PluginManager` 是一个单例类，负责插件的加载、查询、卸载和移除。

### 公开接口

```cpp
class PluginManager : public QObject
{
public:
    static PluginManager *instance();    // 获取单例

    // 加载指定目录下的所有 .so/.dll 插件
    void loadPlugins(const QString &pluginDir);

    // 清除所有已加载插件并重新扫描加载
    void reloadPlugins();

    // 查询已加载的插件
    QList<DevicePluginInterface*> plugins() const;
    DevicePluginInterface *pluginForType(const QString &deviceType) const;
    QStringList deviceTypes() const;

    // 获取插件对应的 .so 文件路径
    QString pluginFilePath(DevicePluginInterface *plugin) const;

    // 从内存卸载插件并删除 .so 文件
    bool removePlugin(DevicePluginInterface *plugin);

    // 注入宿主服务（会追溯性地初始化所有已加载的插件）
    void setHostServices(HostServices *services);

signals:
    void pluginLoaded(const QString &deviceType);
    void pluginError(const QString &filePath, const QString &error);
};
```

### 内部数据结构

| 成员 | 类型 | 说明 |
|---|---|---|
| `m_plugins` | `QList<DevicePluginInterface*>` | 已加载插件列表 |
| `m_pluginMap` | `QMap<QString, DevicePluginInterface*>` | deviceType → 插件映射 |
| `m_pluginInstances` | `QList<QObject*>` | 插件 QObject 实例（用于内存管理） |
| `m_pluginPathMap` | `QMap<DevicePluginInterface*, QString>` | 插件 → .so 文件路径映射 |
| `m_services` | `HostServices*` | 缓存的宿主服务指针 |
| `m_pluginDir` | `QString` | 插件加载目录路径 |

### 关键行为

**加载流程 (`loadPlugins`)**：
1. 扫描目录下所有 `.so`（Linux）或 `.dll`（Windows）文件
2. 用 `QPluginLoader` 逐个加载
3. 通过 `qobject_cast<DevicePluginInterface*>` 验证是否实现了接口
4. 如果 `m_services` 已设置，调用 `initialize(m_services)` 初始化插件
5. 记录插件到四个数据结构中，发射 `pluginLoaded` 信号

**卸载流程 (`removePlugin`)**：
1. 调用 `initialize(nullptr)` 通知插件关闭
2. 从内存中移除并删除 QObject 实例
3. 从所有映射中移除记录
4. 调用 `QFile::remove()` 删除磁盘上的 `.so` 文件

**重载流程 (`reloadPlugins`)**：
1. 对所有已加载插件调用 `initialize(nullptr)`
2. `qDeleteAll` 释放所有 QObject 实例
3. 清空所有映射
4. 重新调用 `loadPlugins(m_pluginDir)`

**关闭协议**：`initialize(nullptr)` 被用作统一的关闭信号，在析构器、`reloadPlugins`、`removePlugin` 中都会调用。

---

## 插件生命周期与管理流程

### 应用启动时的插件加载

```
main.cpp 启动
    │
    ├─ 创建 PluginManager 单例
    │
    ├─ 按优先级搜索 plugins/ 目录：
    │   1. applicationDirPath() + "/plugins"
    │   2. currentPath() + "/plugins"
    │   3. applicationDirPath() + "/../plugins"
    │   4. currentPath() + "/../plugins"
    │
    ├─ 对第一个找到且包含 .so 的目录调用 loadPlugins()
    │
    ├─ 登录成功后创建 Widget
    │
    └─ Widget 构造器中：
        ├─ 初始化 HostServices（数据库、MQTT、命令函数指针）
        ├─ 调用 setHostServices() → 追溯性初始化所有插件
        └─ 初始化插件管理 Tab（initPluginTab）
```

### 插件管理 Tab 操作

| 操作 | 按钮 | 行为 |
|---|---|---|
| 编译插件 | 「编译插件」 | 扫描 `plugins/` 源码目录，对每个含 `.pro` 的子目录执行 qmake + make |
| 重新加载 | 「重新加载插件」 | 调用 `reloadPlugins()`，重新扫描 .so 文件 |
| 移除插件 | 「移除插件」 | 选中插件后确认，调用 `removePlugin()` 删除 .so 文件并刷新列表 |

### 编译流程

```
点击「编译插件」
    │
    ├─ 查找源码目录 plugins/（通过 hasProFiles 验证含 .pro 文件）
    ├─ 收集所有子目录的 .pro 文件到 m_compileQueue
    ├─ 查找 qmake 路径（按优先级搜索多个位置）
    ├─ 从源码目录推导构建输出目录：projectRoot/build/plugins/
    │
    └─ 逐个编译（异步 QProcess 链）：
        qmake .pro → 生成 Makefile
            │
            └─ make -j$(nproc) → 生成 .so
                │
                └─ 全部完成后调用 refreshPluginList() → reloadPlugins()
```

---

## 从零开始开发一个插件

以开发一个"窗帘"插件为例，完整演示开发流程。

### 第一步：创建目录和文件

```
plugins/curtainplugin/
├── curtainplugin.h        # 插件类头文件
├── curtainplugin.cpp      # 插件类实现
├── curtainplugin.json     # 插件元数据
├── curtainplugin.pro      # qmake 项目文件
├── curtaincontrol.h       # 控制界面头文件
├── curtaincontrol.cpp     # 控制界面实现
└── curtaincontrol.ui      # 控制界面 UI 文件（可选，也可纯代码构建）
```

### 第二步：创建插件元数据文件

`curtainplugin.json`:
```json
{
    "Keys": ["curtainplugin"]
}
```

文件名必须与 `.pro` 文件中 `Q_PLUGIN_METADATA` 宏的 `FILE` 参数一致。

### 第三步：实现插件类

`curtainplugin.h`:
```cpp
#ifndef CURTAINPLUGIN_H
#define CURTAINPLUGIN_H

#include <QObject>
#include "deviceplugininterface.h"

class CurtainPlugin : public QObject, public DevicePluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DevicePluginInterface)
    Q_PLUGIN_METADATA(IID DevicePluginInterface_iid FILE "curtainplugin.json")

public:
    explicit CurtainPlugin(QObject *parent = nullptr);

    // 元数据
    QString deviceType() const override;
    QString displayName() const override;
    QString description() const override;
    QString iconText() const override;
    QString mqttDeviceType() const override;

    // 生命周期
    void initialize(HostServices *services) override;

    // 控件工厂
    QWidget *createControlWidget(const QString &roomName,
                                 const QString &deviceName,
                                 QWidget *parent) override;

    // 情景模式
    QWidget *createSceneStateEditor(QWidget *parent) override;
    QString sceneEditorResult(QWidget *editor) const override;

    // 命令构建
    QList<QJsonObject> buildCommands(const QString &state) const override;

    // 状态显示
    QString stateDisplayText(const QString &state) const override;

    // 设备添加（可选，不重写则使用默认实现）
    bool supportsScan() const override;
    bool showAddDeviceDialog(QWidget *parent, const QString &roomName,
                             const QString &username) override;

private:
    HostServices *m_services = nullptr;
};

#endif // CURTAINPLUGIN_H
```

`curtainplugin.cpp`:
```cpp
#include "curtainplugin.h"
#include "curtaincontrol.h"
#include "database.h"
#include "devicediscovery.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>

CurtainPlugin::CurtainPlugin(QObject *parent)
    : QObject(parent)
{
}

// ---- 元数据 ----

QString CurtainPlugin::deviceType() const { return "窗帘"; }
QString CurtainPlugin::displayName() const { return "窗帘控制"; }
QString CurtainPlugin::description() const { return "电动窗帘的开合控制"; }
QString CurtainPlugin::iconText() const { return "\xF0\x9F\xAA\x9F"; }  // 🪟
QString CurtainPlugin::mqttDeviceType() const { return "curtain_device"; }

// ---- 生命周期 ----

void CurtainPlugin::initialize(HostServices *services)
{
    m_services = services;
}

// ---- 控件工厂 ----

QWidget *CurtainPlugin::createControlWidget(const QString &roomName,
                                            const QString &deviceName,
                                            QWidget *parent)
{
    CurtainControl *control = new CurtainControl(parent);
    if (m_services) {
        control->setHostServices(m_services);
    }
    control->setDeviceInfo(roomName, deviceName);
    return control;
}

// ---- 情景模式 ----

QWidget *CurtainPlugin::createSceneStateEditor(QWidget *parent)
{
    QWidget *editor = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editor);

    QComboBox *cb = new QComboBox(editor);
    cb->addItems({"全开", "半开", "关闭"});
    cb->setObjectName("positionCombo");

    layout->addWidget(new QLabel("窗帘位置:"));
    layout->addWidget(cb);

    return editor;
}

QString CurtainPlugin::sceneEditorResult(QWidget *editor) const
{
    QComboBox *cb = editor->findChild<QComboBox*>("positionCombo");
    if (!cb) return "";
    return cb->currentText();
}

// ---- 命令构建 ----

QList<QJsonObject> CurtainPlugin::buildCommands(const QString &state) const
{
    QList<QJsonObject> commands;
    if (state.isEmpty()) return commands;

    QJsonObject cmd;
    cmd["type"] = "position";
    cmd["value"] = state;  // "全开"、"半开"、"关闭"
    commands.append(cmd);

    return commands;
}

// ---- 状态显示 ----

QString CurtainPlugin::stateDisplayText(const QString &state) const
{
    if (state.isEmpty() || state == "未设置") return "未知";
    if (state == "全开") return "全开";
    if (state == "半开") return "半开";
    if (state == "关闭") return "已关闭";
    return state;
}

// ---- 设备添加 ----

bool CurtainPlugin::supportsScan() const
{
    return true;
}

bool CurtainPlugin::showAddDeviceDialog(QWidget *parent,
                                         const QString &roomName,
                                         const QString &username)
{
    // 参考 acplugin 或 sensorplugin 的实现
    // 1. 检查 MQTT 连接
    // 2. 创建扫描对话框
    // 3. 通过 discovery->requestDeviceScan() 触发扫描
    // 4. 过滤 deviceType == "curtain_device" 的设备
    // 5. 调用 db.createDevice() 将选中的设备写入数据库
    // ...
    return false;
}
```

### 第四步：创建控制界面

`curtaincontrol.h`:
```cpp
#ifndef CURTAINCONTROL_H
#define CURTAINCONTROL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>

struct HostServices;

class CurtainControl : public QWidget
{
    Q_OBJECT

public:
    explicit CurtainControl(QWidget *parent = nullptr);
    void setHostServices(HostServices *services);
    void setDeviceInfo(const QString &roomName, const QString &deviceName);
    void loadState();

private slots:
    void onOpenClicked();
    void onHalfClicked();
    void onCloseClicked();

private:
    void saveState(const QString &state);
    void sendPositionCommand(const QString &position);

    HostServices *m_services = nullptr;
    QString m_roomName;
    QString m_deviceName;
    QString m_deviceId;
    QLabel *m_stateLabel;
};

#endif // CURTAINCONTROL_H
```

`curtaincontrol.cpp`:
```cpp
#include "curtaincontrol.h"
#include "database.h"
#include "deviceplugininterface.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonObject>

CurtainControl::CurtainControl(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(300, 200);
    setWindowTitle("窗帘控制");

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_stateLabel = new QLabel("状态: 未知", this);
    m_stateLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(m_stateLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *openBtn = new QPushButton("全开", this);
    QPushButton *halfBtn = new QPushButton("半开", this);
    QPushButton *closeBtn = new QPushButton("关闭", this);

    connect(openBtn, &QPushButton::clicked, this, &CurtainControl::onOpenClicked);
    connect(halfBtn, &QPushButton::clicked, this, &CurtainControl::onHalfClicked);
    connect(closeBtn, &QPushButton::clicked, this, &CurtainControl::onCloseClicked);

    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(halfBtn);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}

void CurtainControl::setHostServices(HostServices *services)
{
    m_services = services;
}

void CurtainControl::setDeviceInfo(const QString &roomName, const QString &deviceName)
{
    m_roomName = roomName;
    m_deviceName = deviceName;
    setWindowTitle(deviceName + " - 窗帘控制");

    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        Database::DeviceInfo info = db.getDeviceInfo(userId, roomName, deviceName);
        m_deviceId = info.deviceId;
    }

    loadState();
}

void CurtainControl::loadState()
{
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        QString state = db.getDeviceState(userId, m_roomName, m_deviceName);
        if (!state.isEmpty()) {
            m_stateLabel->setText("状态: " + state);
        }
    }
}

void CurtainControl::onOpenClicked()
{
    saveState("全开");
    sendPositionCommand("open");
}

void CurtainControl::onHalfClicked()
{
    saveState("半开");
    sendPositionCommand("half");
}

void CurtainControl::onCloseClicked()
{
    saveState("关闭");
    sendPositionCommand("close");
}

void CurtainControl::saveState(const QString &state)
{
    m_stateLabel->setText("状态: " + state);
    Database db;
    if (db.open()) {
        QString username = m_services ? m_services->currentUsername : QString();
        int userId = db.getUserId(username);
        db.updateDeviceState(userId, m_roomName, m_deviceName, state);
    }
}

void CurtainControl::sendPositionCommand(const QString &position)
{
    if (!m_services || !m_services->sendCommand || m_deviceId.isEmpty()) return;

    QJsonObject cmd;
    cmd["type"] = "position";
    cmd["value"] = position;
    m_services->sendCommand(m_deviceId, cmd);
}
```

### 第五步：创建 .pro 文件

`curtainplugin.pro`:
```qmake
QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = curtainplugin

# 包含插件接口和主项目头文件路径
INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

# 插件源文件
HEADERS += \
    curtainplugin.h \
    curtaincontrol.h

SOURCES += \
    curtainplugin.cpp \
    curtaincontrol.cpp

# 引用主项目的依赖源码（根据插件需要选择）
SOURCES += \
    $$PWD/../../database.cpp \
    $$PWD/../../devicediscovery.cpp \
    $$PWD/../../mqttclient.cpp

HEADERS += \
    $$PWD/../../database.h \
    $$PWD/../../devicediscovery.h \
    $$PWD/../../mqttclient.h

# 输出到上级目录（与主程序的 plugins/ 目录对应）
DESTDIR = $$OUT_PWD/..
```

> **注意**：`INCLUDEPATH` 和 `SOURCES` 中的 `$$PWD/../../` 路径是相对于 `.pro` 文件位置的。插件位于 `plugins/curtainplugin/`，所以 `../../` 指向项目根目录。

### 第六步：构建与测试

有两种方式构建插件：

**方式一：通过主程序 UI（推荐）**

1. 将插件源码放入 `plugins/curtainplugin/` 目录
2. 启动 HomeMind 主程序
3. 切换到「插件管理」标签页
4. 点击「编译插件」— 程序自动扫描 `plugins/` 目录并编译所有插件
5. 编译完成后自动加载，插件出现在列表中

**方式二：命令行手动构建**

```bash
mkdir -p build/plugins/curtainplugin
cd build/plugins/curtainplugin
qmake /path/to/plugins/curtainplugin/curtainplugin.pro
make -j$(nproc)
```

编译成功后，`libcurtainplugin.so` 会输出到 `build/plugins/` 目录。点击「重新加载插件」即可加载。

---

## 现有插件分析

### 灯光插件（lightplugin）— 最简实现

最简单的插件示例，推荐作为开发参考。

| 文件 | 说明 |
|---|---|
| `lightplugin.h/.cpp` | 插件类，实现所有接口方法 |
| `lightcontrol.h/.cpp/.ui` | 控制界面，提供开关按钮 |
| `lightplugin.json` | 插件元数据 |
| `lightplugin.pro` | 构建配置 |

**关键特点：**
- 控制界面使用 `.ui` 文件设计
- 状态只有两种：`"开启"` / `"关闭"`
- 不支持扫描添加（`supportsScan()` 使用默认值 `false`）
- 情景编辑器是一个简单的下拉框

### 空调插件（acplugin）— 完整实现

功能最完整的插件示例。

| 文件 | 说明 |
|---|---|
| `acplugin.h/.cpp` | 插件类 |
| `accontrol.h/.cpp/.ui` | 控制界面 |
| `acplugin.json` | 插件元数据 |
| `acplugin.pro` | 构建配置 |

**关键特点：**
- 状态为复合格式：`"开启, 26°C"`
- `buildCommands()` 解析状态字符串，生成多条 MQTT 命令
- `supportsScan()` 返回 `true`，实现了完整的扫描添加对话框
- 情景编辑器包含开关和温度两个控件

### 传感器插件（sensorplugin）— 只读设备

展示如何实现只读类型的设备插件。

| 文件 | 说明 |
|---|---|
| `sensorplugin.h/.cpp` | 插件类 |
| `sensorcontrol.h/.cpp` | 控制界面（纯代码构建，无 .ui 文件） |
| `sensorplugin.json` | 插件元数据 |
| `sensorplugin.pro` | 构建配置 |

**关键特点：**
- `buildCommands()` 返回空列表（传感器只读，不发送命令）
- `createSceneStateEditor()` 返回 `nullptr`（传感器不参与情景）
- 控制界面纯代码构建，定时轮询传感器数据
- 使用 `HostServices::fetchSensorDataAsync` 获取实时数据

---

## 构建与调试

### 插件 .pro 文件结构说明

```qmake
QT += widgets sql network       # 依赖 Qt 模块
CONFIG += c++17 plugin          # C++17 + 插件模式
TEMPLATE = lib                  # 构建为共享库
TARGET = myplugin               # 输出文件名（libmyplugin.so）

INCLUDEPATH += $$PWD/../../plugin    # 插件接口头文件
INCLUDEPATH += $$PWD/../..           # 主项目头文件

# 插件自身的源文件
HEADERS += myplugin.h mycontrol.h
SOURCES += myplugin.cpp mycontrol.cpp

# 主项目的依赖源码（每个插件独立编译，不依赖主程序的 .o）
SOURCES += $$PWD/../../database.cpp \
           $$PWD/../../devicediscovery.cpp \
           $$PWD/../../mqttclient.cpp
HEADERS += $$PWD/../../database.h \
           $$PWD/../../devicediscovery.h \
           $$PWD/../../mqttclient.h

DESTDIR = $$OUT_PWD/..          # .so 输出到 build/plugins/
```

> 每个插件会将 `database.cpp`、`devicediscovery.cpp`、`mqttclient.cpp` 独立编译进自己的 `.so` 中，形成自包含的共享库。这避免了 ABI 耦合，但意味着这些宿主类在每个插件中各有一份副本。

### 调试

- 插件的 `qDebug()` 输出会显示在主程序的控制台中
- `PluginManager` 加载插件时会打印日志：
  ```
  PluginManager: loaded plugin 空调 (空调控制) from libacplugin.so
  ```
- 如果加载失败，会打印错误：
  ```
  PluginManager: failed to load libxxx.so: <error string>
  PluginManager: libxxx.so does not implement DevicePluginInterface
  ```

### 常见问题

| 问题 | 原因 | 解决 |
|---|---|---|
| 插件未出现在列表中 | `.so` 文件不在插件目录 | 确认 `build/plugins/` 下有 `.so` 文件，或点击「重新加载插件」 |
| 链接错误 "undefined reference" | 头文件声明了 `override` 但未实现 | 补充缺失的方法实现 |
| `Q_PLUGIN_METADATA` 编译错误 | `.json` 文件路径或内容错误 | 确认 json 文件存在且格式正确 |
| 插件加载但控制界面不显示 | `createControlWidget()` 返回 `nullptr` | 检查控件创建逻辑 |
| 编译插件失败 | 未安装 Qt 开发环境或 qmake 不在 PATH | 确保 `~/Qt/5.15.2/gcc_64/bin/qmake` 存在 |
| 移除插件后仍显示 | 需要刷新列表 | 移除操作会自动刷新，如未生效点击「重新加载插件」 |

---

## 注意事项

### 1. override 声明与实现必须匹配

如果在头文件中声明了 `override`，就必须在 `.cpp` 中提供实现，否则会链接失败。

```cpp
// 错误：声明了 override 但忘记实现 → 链接错误
bool supportsScan() const override;

// 正确：不声明，使用基类默认实现
// （不写这一行即可）

// 正确：声明并实现
bool supportsScan() const override;  // .cpp 中提供 { return true; }
```

### 2. 数据库访问

插件可以直接使用 `Database` 类，需要在 `.pro` 中包含 `database.cpp`：

```qmake
SOURCES += $$PWD/../../database.cpp
HEADERS += $$PWD/../../database.h
```

### 3. 线程安全

插件的控件方法都在主线程（GUI 线程）中调用，不需要额外加锁。但如果使用 `fetchSensorDataAsync`，注意它可能涉及网络 I/O。

### 4. 插件依赖

插件需要在 `.pro` 中引用主项目的以下源码（根据实际使用选择）：

| 源码 | 用途 |
|---|---|
| `database.cpp/.h` | 数据库读写 |
| `devicediscovery.cpp/.h` | 设备扫描发现 |
| `mqttclient.cpp/.h` | MQTT 通信（devicediscovery 依赖） |

### 5. 状态字符串约定

状态字符串用于数据库存储和情景模式，建议遵循以下格式：

- 简单设备：`"开启"` / `"关闭"`
- 复合状态：`"开启, 26°C"`（用逗号分隔）
- 多值状态：`"全开"` / `"半开"` / `"关闭"`

`buildCommands()` 和 `stateDisplayText()` 需要能正确解析你定义的格式。

### 6. 插件关闭协议

当主程序调用 `initialize(nullptr)` 时，表示插件即将被卸载。插件应在此时：
- 断开所有信号连接
- 释放网络资源
- 保存未持久化的状态

```cpp
void MyPlugin::initialize(HostServices *services)
{
    if (!services) {
        // 关闭信号：清理资源
        // 断开连接、释放资源等
        return;
    }
    m_services = services;
    // 正常初始化
}
```
