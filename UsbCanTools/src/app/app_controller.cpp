// 文件说明：应用控制器实现，负责 UI 与底层 CAN 线程解耦。
#include "app/app_controller.h"

#include "can/can_worker_api.h"

AppController::AppController(QObject *parent)
    : QObject(parent)
{
}

AppController::~AppController()
{
    teardownWorker();
}

// 连接设备：先断开旧连接，再新建并启动 worker。
bool AppController::connectDevice(int deviceType, int deviceIndex, int canIndex, int baudIndex, int mode)
{
    if (m_worker)
        disconnectDevice();

    m_worker = new CanWorker(this);
    bindWorkerSignals(m_worker);

    if (!m_worker->openDevice(deviceType, deviceIndex, canIndex, baudIndex, mode)) {
        teardownWorker();
        return false;
    }

    m_worker->start();
    return true;
}

// 断开设备并重置状态文本。
void AppController::disconnectDevice()
{
    teardownWorker();
    m_boardInfo = QStringLiteral("未连接");
    m_busStatus = QStringLiteral("--");
    emit boardInfoChanged(m_boardInfo);
    emit busStatusChanged(m_busStatus);
    emit connectionChanged(false);
}

// 返回当前连接状态。
bool AppController::isConnected() const
{
    return m_worker && m_worker->isDeviceOpen();
}

// 返回缓存板卡信息。
QString AppController::boardInfo() const
{
    return m_boardInfo;
}

// 返回缓存总线状态。
QString AppController::busStatus() const
{
    return m_busStatus;
}

// 获取底层 worker 指针。
CanWorker *AppController::worker() const
{
    return m_worker;
}

// 设备打开成功处理。
void AppController::onDeviceOpened(const QString &info)
{
    m_boardInfo = info;
    emit boardInfoChanged(m_boardInfo);
    emit connectionChanged(true);
}

// 设备关闭处理。
void AppController::onDeviceClosed()
{
    m_busStatus = QStringLiteral("--");
    emit busStatusChanged(m_busStatus);
    emit connectionChanged(false);
}

// 打开设备失败处理。
void AppController::onOpenFailed(const QString &reason)
{
    emit openFailed(reason);
}

// 绑定 worker 信号到控制器转发。
void AppController::bindWorkerSignals(CanWorker *worker)
{
    connect(worker, &CanWorker::deviceOpened, this, &AppController::onDeviceOpened);
    connect(worker, &CanWorker::deviceClosed, this, &AppController::onDeviceClosed);
    connect(worker, &CanWorker::openFailed, this, &AppController::onOpenFailed);
    connect(worker, &CanWorker::frameReceived, this, &AppController::frameReceived);
}

// 安全停止并释放 worker。
void AppController::teardownWorker()
{
    if (!m_worker)
        return;

    disconnect(m_worker, nullptr, this, nullptr);
    m_worker->stopWorker();
    if (m_worker->isRunning())
        m_worker->wait(3000);
    m_worker->closeDevice();
    m_worker->deleteLater();
    m_worker = nullptr;
}
