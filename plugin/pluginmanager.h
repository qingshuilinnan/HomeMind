#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include "deviceplugininterface.h"

class PluginManager : public QObject
{
    Q_OBJECT
public:
    static PluginManager *instance();

    void loadPlugins(const QString &pluginDir);
    void reloadPlugins();

    QList<DevicePluginInterface*> plugins() const;
    DevicePluginInterface *pluginForType(const QString &deviceType) const;
    QStringList deviceTypes() const;
    QString pluginFilePath(DevicePluginInterface *plugin) const;
    bool removePlugin(DevicePluginInterface *plugin);

    void setHostServices(HostServices *services);

signals:
    void pluginLoaded(const QString &deviceType);
    void pluginError(const QString &filePath, const QString &error);

private:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager();

    static PluginManager *s_instance;
    QList<DevicePluginInterface*> m_plugins;
    QMap<QString, DevicePluginInterface*> m_pluginMap;
    QList<QObject*> m_pluginInstances;
    QMap<DevicePluginInterface*, QString> m_pluginPathMap;
    HostServices *m_services = nullptr;
    QString m_pluginDir;
};

#endif // PLUGINMANAGER_H
