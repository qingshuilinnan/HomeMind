#ifndef ACPLUGIN_H
#define ACPLUGIN_H

#include <QObject>
#include "deviceplugininterface.h"

class ACPlugin : public QObject, public DevicePluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DevicePluginInterface)
    Q_PLUGIN_METADATA(IID DevicePluginInterface_iid FILE "acplugin.json")

public:
    explicit ACPlugin(QObject *parent = nullptr);

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

    bool supportsScan() const override;
    bool showAddDeviceDialog(QWidget *parent, const QString &roomName, const QString &username) override;
    bool showManualAddDialog(QWidget *parent, const QString &roomName, const QString &username) override;

private:
    HostServices *m_services = nullptr;
};

#endif // ACPLUGIN_H
