// 文件说明：应用控制器模块，统一管理设备连接生命周期并向界面广播状态。
#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <QObject>
#include <QString>

class CanWorker;

// 应用控制器：解耦 UI 与底层 CanWorker，提供统一连接状态接口。
class AppController : public QObject
{
    Q_OBJECT
public:
    // 构造应用控制器。
    explicit AppController(QObject *parent = nullptr);
    // 析构应用控制器并回收底层工作线程。
    ~AppController() override;

    // 连接设备并启动收发线程。
    bool connectDevice(int deviceType, int deviceIndex, int canIndex, int baudIndex, int mode);
    // 断开设备并释放资源。
    void disconnectDevice();

    // 是否已连接设备。
    bool isConnected() const;
    // 当前板卡信息文本。
    QString boardInfo() const;
    // 当前总线状态文本。
    QString busStatus() const;
    // 获取底层 CanWorker 指针（供高级页面调用）。
    CanWorker *worker() const;

signals:
    // 连接状态变化信号。
    void connectionChanged(bool connected);
    // 板卡信息变化信号。
    void boardInfoChanged(const QString &info);
    // 总线状态变化信号。
    void busStatusChanged(const QString &status);
    // 打开设备失败信号。
    void openFailed(const QString &reason);
    // 收发帧转发信号（直接透传底层帧事件）。
    void frameReceived(const QString &timeMs, const QString &dir, const QString &idHex,
                       const QString &frameType, const QString &dlc, const QString &dataHex);

private slots:
    // 设备打开成功处理。
    void onDeviceOpened(const QString &info);
    // 设备关闭处理。
    void onDeviceClosed();
    // 设备打开失败处理。
    void onOpenFailed(const QString &reason);

private:
    // 绑定底层 worker 相关信号。
    void bindWorkerSignals(CanWorker *worker);
    // 停止并释放底层 worker。
    void teardownWorker();

    // 当前设备工作线程。
    CanWorker *m_worker = nullptr;
    // 缓存的板卡信息文本。
    QString m_boardInfo;
    // 缓存的总线状态文本。
    QString m_busStatus;
};

#endif // APP_CONTROLLER_H
