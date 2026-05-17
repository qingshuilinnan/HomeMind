#ifndef ROOMDETAIL_H
#define ROOMDETAIL_H

#include <QWidget>
#include <QListWidget>
#include <QMap>
#include <QProcess>

QT_BEGIN_NAMESPACE
namespace Ui {
class RoomDetail;
}
QT_END_NAMESPACE

class DevicePluginInterface;
class QPushButton;
class QLabel;

class RoomDetail : public QWidget
{
    Q_OBJECT

signals:
    void devicesChanged();

public:
    explicit RoomDetail(QWidget *parent = nullptr);
    ~RoomDetail();

    void setRoomName(const QString &name);
    void loadDevicesFromDatabase();

private slots:
    void on_addDeviceButton_clicked();
    void onQmakeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onMakeFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    Ui::RoomDetail *ui;
    QString roomName;
    QMap<QListWidget*, DevicePluginInterface*> m_listPluginMap;

    void addDeviceItem(QListWidget *listWidget, const QString &deviceName, const QString &deviceType);
    void addAddDeviceItem(QListWidget *listWidget, const QString &deviceType);
    void showAddDeviceDialog(const QString &deviceType);
    void showEmptyState();
    void rebuildFromPlugins();
    void startCompilePlugins();

    // 编译插件相关
    QPushButton *m_compileBtn = nullptr;
    QLabel *m_compileStatus = nullptr;
    QStringList m_compileQueue;
    int m_compileIndex = 0;
    QString m_currentBuildDir;
    void compileNextPlugin();
    void runMake(const QString &buildDir);
};
#endif // ROOMDETAIL_H
