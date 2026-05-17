#include "pluginmanager.h"
#include <QDir>
#include <QPluginLoader>
#include <QDebug>
#include <QFile>

PluginManager *PluginManager::s_instance = nullptr;

PluginManager *PluginManager::instance()
{
    if (!s_instance) {
        s_instance = new PluginManager();
    }
    return s_instance;
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

PluginManager::~PluginManager()
{
    for (auto *plugin : m_plugins) {
        plugin->initialize(nullptr); // signal shutdown via nullptr services
    }
    qDeleteAll(m_pluginInstances);
}

void PluginManager::loadPlugins(const QString &pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        qWarning() << "PluginManager: plugin directory not found:" << pluginDir;
        return;
    }

    m_pluginDir = pluginDir;

    QStringList filters;
#if defined(Q_OS_WIN)
    filters << "*.dll";
#else
    filters << "*.so";
#endif

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &fi : files) {
        QPluginLoader loader(fi.absoluteFilePath());
        QObject *instance = loader.instance();
        if (!instance) {
            qWarning() << "PluginManager: failed to load" << fi.fileName() << ":" << loader.errorString();
            emit pluginError(fi.fileName(), loader.errorString());
            continue;
        }

        DevicePluginInterface *plugin = qobject_cast<DevicePluginInterface*>(instance);
        if (!plugin) {
            qWarning() << "PluginManager:" << fi.fileName() << "does not implement DevicePluginInterface";
            delete instance;
            continue;
        }

        if (m_services) {
            plugin->initialize(m_services);
        }

        m_pluginInstances.append(instance);
        m_plugins.append(plugin);
        m_pluginMap.insert(plugin->deviceType(), plugin);
        m_pluginPathMap.insert(plugin, fi.absoluteFilePath());

        qDebug() << "PluginManager: loaded plugin" << plugin->deviceType()
                 << "(" << plugin->displayName() << ") from" << fi.fileName();
        emit pluginLoaded(plugin->deviceType());
    }
}

QList<DevicePluginInterface*> PluginManager::plugins() const
{
    return m_plugins;
}

DevicePluginInterface *PluginManager::pluginForType(const QString &deviceType) const
{
    return m_pluginMap.value(deviceType, nullptr);
}

QStringList PluginManager::deviceTypes() const
{
    return m_pluginMap.keys();
}

QString PluginManager::pluginFilePath(DevicePluginInterface *plugin) const
{
    return m_pluginPathMap.value(plugin, QString());
}

bool PluginManager::removePlugin(DevicePluginInterface *plugin)
{
    QString filePath = m_pluginPathMap.value(plugin);
    if (filePath.isEmpty()) return false;

    plugin->initialize(nullptr);

    // 从实例列表中找到并移除对应的 QObject
    for (int i = 0; i < m_pluginInstances.size(); ++i) {
        DevicePluginInterface *p = qobject_cast<DevicePluginInterface*>(m_pluginInstances[i]);
        if (p == plugin) {
            delete m_pluginInstances.takeAt(i);
            break;
        }
    }

    m_plugins.removeOne(plugin);
    m_pluginMap.remove(plugin->deviceType());
    m_pluginPathMap.remove(plugin);

    return QFile::remove(filePath);
}

void PluginManager::setHostServices(HostServices *services)
{
    m_services = services;
    for (auto *plugin : m_plugins) {
        plugin->initialize(services);
    }
}

void PluginManager::reloadPlugins()
{
    // 清理已有插件
    for (auto *plugin : m_plugins) {
        plugin->initialize(nullptr);
    }
    qDeleteAll(m_pluginInstances);
    m_pluginInstances.clear();
    m_plugins.clear();
    m_pluginMap.clear();
    m_pluginPathMap.clear();

    // 重新加载
    if (!m_pluginDir.isEmpty()) {
        loadPlugins(m_pluginDir);
    }
}
