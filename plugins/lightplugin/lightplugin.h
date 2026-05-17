#ifndef LIGHTPLUGIN_H
#define LIGHTPLUGIN_H

#include <QObject>
#include "deviceplugininterface.h"

class LightPlugin : public QObject, public DevicePluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DevicePluginInterface)
    Q_PLUGIN_METADATA(IID DevicePluginInterface_iid FILE "lightplugin.json")

public:
    explicit LightPlugin(QObject *parent = nullptr);

    QString deviceType() const override;
    QString displayName() const override;
    QString description() const override;
    QString iconText() const override;
    QString mqttDeviceType() const override;

    void initialize(HostServices *services) override;

    QWidget *createControlWidget(const QString &roomName, const QString &deviceName, QWidget *parent) override;

    QList<QJsonObject> buildCommands(const QString &state) const override;

    QWidget *createSceneStateEditor(QWidget *parent) override;
    QString sceneEditorResult(QWidget *editor) const override;

    QString stateDisplayText(const QString &state) const override;

private:
    HostServices *m_services = nullptr;
};

#endif // LIGHTPLUGIN_H
