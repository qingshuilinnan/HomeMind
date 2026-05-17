# HomeMind

智能家居控制与自动化平台。通过 MQTT 协议管理 IoT 设备，提供桌面客户端和 Web 控制中心，并支持插件化扩展设备类型。

## 功能特性

- **房间管理** — 创建、删除房间，按房间归类设备
- **设备控制** — 空调（红外遥控，支持温度调节）、灯光开关、传感器数据展示、监控设备
- **情景模式** — 一键切换多设备预设状态，支持为每个设备单独配置目标状态
- **自动化任务** — 支持定时触发、室内温度触发、室外温度触发，执行应用场景或控制设备
- **设备发现** — 通过 UDP 广播 + MQTT 自动发现局域网内的 ESP 设备
- **Web 控制中心** — 内置 HTTP 服务器（端口 8080），提供完整的 SPA Web 管理界面
- **插件架构** — 通过 DevicePluginInterface 接口扩展新设备类型
- **用户系统** — 多用户支持，管理员角色，自动登录，密码管理，Web 端 Token 认证

## 技术栈

| 组件 | 技术 |
|---|---|
| 语言 | C++17 |
| GUI 框架 | Qt 5.15.2（Widgets / SQL / Network） |
| 构建系统 | qmake |
| 数据库 | SQLite（通过 Qt QSqlDatabase） |
| 通信协议 | MQTT 3.1.1（自行实现，无外部依赖） / UDP / HTTP |
| Web 前端 | HTML + CSS + JavaScript（单页应用，无外部框架依赖） |
| 硬件固件 | Arduino / ESP8266 |

## 项目结构

```
HomeMind/
├── main.cpp                # 程序入口
├── widget.cpp/.h/.ui       # 主窗口（仪表盘）
├── loginwindow.cpp/.h/.ui  # 登录/注册窗口
├── roomdetail.cpp/.h/.ui   # 房间详情（设备分类与添加）
├── database.cpp/.h         # SQLite 数据层
├── httpserver.cpp/.h       # 内置 HTTP 服务器（REST API + 静态文件）
├── mqttclient.cpp/.h       # MQTT 3.1.1 客户端（原始 TCP 实现）
├── devicediscovery.cpp/.h  # 设备发现（UDP + MQTT）
├── devicecommander.cpp/.h  # 设备指令发送与传感器数据获取
├── style.qss              # Qt 全局样式表
├── resources.qrc           # Qt 资源文件
├── HomeMind.pro            # qmake 项目文件
│
├── plugin/                 # 插件框架
│   ├── deviceplugininterface.h  # 插件接口定义
│   └── pluginmanager.cpp/.h     # 插件加载管理器
│
├── plugins/                # 设备插件（各编译为独立 .so）
│   ├── acplugin/           # 空调插件（红外遥控，温度 16-30°C）
│   ├── lightplugin/        # 灯光插件
│   ├── sensorplugin/       # 传感器插件
│   └── monitorplugin/      # 监控插件
│
├── devices/                # Arduino/ESP 固件
│   ├── air_conditioner/    # 空调控制固件（红外发射）
│   └── temp_humidity_sensor/ # 温湿度传感器固件
│
└── web/                    # Web 前端
    └── index.html          # 单页应用（登录 + 全功能管理界面）
```

## 构建与运行

### 环境要求

- Qt 5.15.2（含 Widgets、SQL、Network 模块）
- GCC（支持 C++17）
- qmake

### 构建主程序

```bash
mkdir build && cd build
qmake ../HomeMind.pro
make -j$(nproc)
```

### 构建插件

插件为独立项目，需单独构建：

```bash
# 构建空调插件
cd build/plugins/acplugin
qmake ../../../plugins/acplugin/acplugin.pro
make -j$(nproc)

# 构建灯光插件
cd ../lightplugin
qmake ../../../plugins/lightplugin/lightplugin.pro
make -j$(nproc)

# 构建传感器插件
cd ../sensorplugin
qmake ../../../plugins/sensorplugin/sensorplugin.pro
make -j$(nproc)

# 构建监控插件
cd ../monitorplugin
qmake ../../../plugins/monitorplugin/monitorplugin.pro
make -j$(nproc)
```

### 运行

确保 `plugins/` 目录（含 `.so` 文件）与可执行文件在同一目录或其父目录下：

```bash
cd build
./HomeMind
```

### 使用 Qt Creator

1. 打开 `HomeMind.pro` 构建并运行主程序
2. 分别打开 `plugins/` 下各插件的 `.pro` 文件，构建插件
3. 插件 `.so` 文件会输出到 `build/plugins/`

## Web 控制中心

启动程序后，通过浏览器访问 `http://localhost:8080` 即可使用 Web 控制中心。

### 功能页面

| 页面 | 功能 |
|---|---|
| **仪表盘** | 环境数据（天气、室内外温度、湿度）、情景模式快速应用、设备概览与控制 |
| **房间管理** | 添加/删除房间，查看房间内设备数量 |
| **设备控制** | 按房间分组的设备列表，开关控制，空调温度调节（16-30°C 滑块），添加/删除设备 |
| **情景模式** | 创建/编辑/删除/应用情景，编辑时可为每个设备设置目标状态 |
| **自动化任务** | 创建/编辑/删除任务，支持时间/室内温度/室外温度触发，执行应用场景或控制设备 |
| **设置** | 修改密码、管理员用户管理、退出登录 |

### REST API

Web 前端通过以下 REST API 与后端通信（端口 8080）：

| 端点 | 方法 | 说明 | 认证 |
|---|---|---|---|
| `/api/login` | POST | 用户登录，返回 Token | 否 |
| `/api/register` | POST | 用户注册 | 否 |
| `/api/logout` | POST | 用户登出 | 是 |
| `/api/status` | 获取环境数据和用户信息 | GET | 是 |
| `/api/devices` | GET | 获取所有设备列表 | 是 |
| `/api/devices/create` | POST | 添加设备 | 是 |
| `/api/devices/delete` | POST | 删除设备 | 是 |
| `/api/control` | POST | 控制设备状态 | 是 |
| `/api/rooms` | GET | 获取房间列表 | 是 |
| `/api/rooms/create` | POST | 创建房间 | 是 |
| `/api/rooms/delete` | POST | 删除房间 | 是 |
| `/api/scenes` | GET | 获取情景列表 | 是 |
| `/api/scenes/create` | POST | 创建情景 | 是 |
| `/api/scenes/delete` | POST | 删除情景 | 是 |
| `/api/scenes/devices` | GET | 获取情景设备配置 | 是 |
| `/api/scenes/devices/save` | POST | 保存情景设备配置 | 是 |
| `/api/apply_scene` | POST | 应用情景模式 | 是 |
| `/api/tasks` | GET | 获取任务列表 | 是 |
| `/api/tasks/create` | POST | 创建任务 | 是 |
| `/api/tasks/update` | POST | 更新任务 | 是 |
| `/api/tasks/delete` | POST | 删除任务 | 是 |
| `/api/change_password` | POST | 修改密码 | 是 |
| `/api/users` | GET | 获取用户列表（管理员） | 是 |
| `/api/users/delete` | POST | 删除用户（管理员） | 是 |

认证方式：请求头 `Authorization: Bearer <token>`

## 硬件固件

### 空调控制器（`devices/air_conditioner/`）

- 基于 ESP8266
- 通过红外发射模块控制空调
- 连接 WiFi 后通过 MQTT 接收指令

### 温湿度传感器（`devices/temp_humidity_sensor/`）

- 基于 ESP8266
- DHT11/DHT22 温湿度传感器
- 光照传感器、PIR 人体感应
- 通过 MQTT 上报数据

## 插件开发

实现 `DevicePluginInterface` 接口即可添加新设备类型：

```cpp
#include "deviceplugininterface.h"

class MyPlugin : public QObject, public DevicePluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DevicePluginInterface)
    Q_PLUGIN_METADATA(IID DevicePluginInterface_iid FILE "myplugin.json")

public:
    QString deviceType() const override;      // 设备类型标识
    QString displayName() const override;     // 显示名称
    QString description() const override;     // 描述
    QString iconText() const override;        // 图标文本
    QString mqttDeviceType() const override;  // MQTT 设备类型标识

    void initialize(HostServices *services) override;
    QWidget *createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent) override;
    QList<QJsonObject> buildCommands(const QString &state) const override;
    QWidget *createSceneStateEditor(QWidget *parent) override;
    QString sceneEditorResult(QWidget *editor) const override;
    QString stateDisplayText(const QString &state) const override;
    bool showAddDeviceDialog(QWidget *parent, const QString &roomName, const QString &username) override;
};
```

插件 `.pro` 文件示例：

```qmake
QT += widgets sql network
CONFIG += c++17 plugin
TEMPLATE = lib
TARGET = myplugin

INCLUDEPATH += $$PWD/../../plugin
INCLUDEPATH += $$PWD/../..

HEADERS += myplugin.h
SOURCES += myplugin.cpp

# 引用主项目的依赖源码
SOURCES += $$PWD/../../database.cpp
HEADERS += $$PWD/../../database.h

DESTDIR = $$OUT_PWD/..
```

## 数据库结构

| 表名 | 用途 |
|---|---|
| `users` | 用户账号（管理员角色、自动登录、密码、位置信息） |
| `rooms` | 房间信息 |
| `devices` | 设备信息（名称、类型、房间、品牌、状态、IP、端口、设备ID） |
| `scenes` | 情景模式 |
| `scene_devices` | 情景与设备状态关联 |
| `tasks` | 自动化任务（定时、温度触发，应用场景/控制设备） |

## 默认账号

| 用户名 | 密码 | 角色 |
|---|---|---|
| admin | admin | 管理员 |
