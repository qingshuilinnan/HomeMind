#include "lightplugin.h"
#include "lightcontrol.h"
#include "database.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>

LightPlugin::LightPlugin(QObject *parent)
    : QObject(parent)
{
}

QString LightPlugin::deviceType() const { return "灯光"; }
QString LightPlugin::displayName() const { return "灯光控制"; }
QString LightPlugin::description() const { return "灯光设备的开关控制"; }
QString LightPlugin::iconText() const { return "\xF0\x9F\x92\xA1"; }
QString LightPlugin::mqttDeviceType() const { return "light_device"; }

void LightPlugin::initialize(HostServices *services)
{
    m_services = services;
}

QWidget *LightPlugin::createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent)
{
    LightControl *control = new LightControl(parent);
    if (m_services) {
        control->setHostServices(m_services);
    }
    control->setDeviceInfo(roomName, deviceName);
    return control;
}

QList<QJsonObject> LightPlugin::buildCommands(const QString &state) const
{
    QList<QJsonObject> commands;
    if (state.isEmpty()) return commands;

    QJsonObject cmd;
    cmd["type"] = "power";
    cmd["value"] = (state == "开启") ? "true" : "false";
    commands.append(cmd);

    return commands;
}

QWidget *LightPlugin::createSceneStateEditor(QWidget *parent)
{
    QWidget *editor = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(editor);

    QComboBox *cb = new QComboBox(editor);
    cb->addItems({"开启", "关闭"});
    cb->setObjectName("powerCombo");

    layout->addWidget(new QLabel("开关状态:"));
    layout->addWidget(cb);

    return editor;
}

QString LightPlugin::sceneEditorResult(QWidget *editor) const
{
    QComboBox *cb = editor->findChild<QComboBox*>("powerCombo");
    if (!cb) return "";
    return cb->currentText();
}

QString LightPlugin::stateDisplayText(const QString &state) const
{
    if (state.isEmpty() || state == "未设置") return "\xE2\x9A\xAA 未知";
    if (state == "开启") return "\xE2\x9A\xAA 运行中";
    if (state == "关闭") return "\xE2\x98\xAA 已关机";
    return "\xE2\x9A\xAA " + state;
}
