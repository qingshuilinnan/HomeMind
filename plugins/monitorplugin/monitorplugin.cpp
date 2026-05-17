#include "monitorplugin.h"
#include "monitorcontrol.h"
#include "database.h"
#include "devicediscovery.h"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>

MonitorPlugin::MonitorPlugin(QObject *parent)
    : QObject(parent)
{
}

QString MonitorPlugin::deviceType() const { return "监控"; }

QString MonitorPlugin::displayName() const { return "监控画面"; }

QString MonitorPlugin::description() const { return "实时视频监控画面查看与控制"; }

QString MonitorPlugin::iconText() const { return "\xF0\x9F\x93\xB9"; }

QString MonitorPlugin::mqttDeviceType() const { return "monitor_device"; }

void MonitorPlugin::initialize(HostServices *services)
{
    m_services = services;
}

QWidget *MonitorPlugin::createControlWidget(const QString &roomName,
                                            const QString &deviceName,
                                            QWidget *parent)
{
    MonitorControl *control = new MonitorControl(parent);
    if (m_services) {
        control->setHostServices(m_services);
    }
    control->setDeviceInfo(roomName, deviceName);
    return control;
}

QWidget *MonitorPlugin::createSceneStateEditor(QWidget *parent)
{
    QWidget *editor = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editor);

    QComboBox *cb = new QComboBox(editor);
    cb->addItems({"录制中", "暂停", "关闭"});
    cb->setObjectName("monitorStateCombo");

    layout->addWidget(new QLabel("监控状态:"));
    layout->addWidget(cb);

    return editor;
}

QString MonitorPlugin::sceneEditorResult(QWidget *editor) const
{
    QComboBox *cb = editor->findChild<QComboBox*>("monitorStateCombo");
    if (!cb) return "";
    return cb->currentText();
}

QList<QJsonObject> MonitorPlugin::buildCommands(const QString &state) const
{
    QList<QJsonObject> commands;
    if (state.isEmpty()) return commands;

    QJsonObject cmd;
    cmd["type"] = "monitor_state";
    cmd["value"] = state;
    commands.append(cmd);

    return commands;
}

QString MonitorPlugin::stateDisplayText(const QString &state) const
{
    if (state.isEmpty() || state == "未设置") return "未知";
    if (state == "录制中") return "正在录制";
    if (state == "暂停") return "已暂停";
    if (state == "关闭") return "已关闭";
    return state;
}

bool MonitorPlugin::supportsScan() const
{
    return false;
}

bool MonitorPlugin::showAddDeviceDialog(QWidget *parent,
                                         const QString &roomName,
                                         const QString &username)
{
    Q_UNUSED(parent)
    Q_UNUSED(roomName)
    Q_UNUSED(username)
    return false;
}
