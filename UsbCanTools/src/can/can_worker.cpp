// 文件说明：CAN 工作线程实现，封装驱动调用、收发轮询与状态维护。
#include "can/can_worker_api.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>

// 按优先级搜索 ECanVci 动态库路径。
static QString resolvedEcanVciLibraryFileName()
{
#ifdef Q_OS_WIN
    const QString dllName = QStringLiteral("ECanVci.dll");
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();

    candidates << QDir(appDir).absoluteFilePath(dllName);
    candidates << QDir(appDir).absoluteFilePath(QStringLiteral("lib/") + dllName);
    candidates << QDir::current().absoluteFilePath(QStringLiteral("lib/") + dllName);

    QDir walk(appDir);
    for (int depth = 0; depth < 14; ++depth) {
        candidates << walk.absoluteFilePath(QStringLiteral("lib/") + dllName);
        if (!walk.cdUp())
            break;
    }

    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
#endif
    return QStringLiteral("ECanVci");
}

// 将字节数组转换为以空格分隔的十六进制字符串。
static QString toHexSpaced(const BYTE *data, UINT len)
{
    QString s;
    for (UINT i = 0; i < len; ++i) {
        if (i)
            s += QLatin1Char(' ');
        s += QString("%1").arg(data[i], 2, 16, QChar('0')).toUpper();
    }
    return s;
}

// 根据帧类型格式化 CAN ID 文本。
static QString formatCanId(UINT id, bool ext)
{
    if (ext)
        return QString("%1").arg(id, 8, 16, QChar('0')).toUpper();
    return QString("%1").arg(id & 0x7FFu, 3, 16, QChar('0')).toUpper();
}

namespace CanVci {

// 生成底层初始化参数。
void fillInitConfig(INIT_CONFIG *cfg, int baudIndex, int mode)
{
    cfg->AccCode = 0x00000000;
    cfg->AccMask = 0xFFFFFFFF;
    cfg->Filter = 0;
    cfg->Reserved = 0;
    cfg->Mode = UCHAR(mode);

    switch (baudIndex) {
    case 0: cfg->Timing0 = 0x00; cfg->Timing1 = 0x14; break;
    case 1: cfg->Timing0 = 0x00; cfg->Timing1 = 0x16; break;
    case 2: cfg->Timing0 = 0x80; cfg->Timing1 = 0xB6; break;
    case 3: cfg->Timing0 = 0x00; cfg->Timing1 = 0x1C; break;
    case 4: cfg->Timing0 = 0x80; cfg->Timing1 = 0xFA; break;
    case 5: cfg->Timing0 = 0x01; cfg->Timing1 = 0x1C; break;
    case 6: cfg->Timing0 = 0x81; cfg->Timing1 = 0xFA; break;
    case 7: cfg->Timing0 = 0x03; cfg->Timing1 = 0x1C; break;
    case 8: cfg->Timing0 = 0x04; cfg->Timing1 = 0x1C; break;
    case 9: cfg->Timing0 = 0x83; cfg->Timing1 = 0xFF; break;
    case 10: cfg->Timing0 = 0x09; cfg->Timing1 = 0x1C; break;
    default: cfg->Timing0 = 0x00; cfg->Timing1 = 0x1C; break;
    }
}

// 波特率索引转显示文本。
QString baudIndexToLabel(int baudIndex)
{
    static const char *const kNames[] = {
        "1000k", "800k", "666k", "500k", "400k", "250k", "200k", "125k", "100k", "80k", "50k",
    };
    const int n = int(sizeof(kNames) / sizeof(kNames[0]));
    if (baudIndex >= 0 && baudIndex < n)
        return QString::fromUtf8(kNames[baudIndex]);
    return QStringLiteral("?");
}

// 返回支持的波特率数量。
int baudLabelCount()
{
    return 11;
}

} // namespace CanVci

CanWorker::CanWorker(QObject *parent)
    : QThread(parent)
{
    resolveApi();
}

CanWorker::~CanWorker()
{
    stopWorker();
    if (isRunning())
        wait(3000);
    closeDevice();
}

// 解析驱动导出函数。
bool CanWorker::resolveApi()
{
    const QString path = resolvedEcanVciLibraryFileName();
    m_lib.setFileName(path);
    if (!m_lib.load()) {
        qWarning() << "ECanVci load failed, path:" << path << m_lib.errorString();
        return false;
    }

    m_OpenDevice = reinterpret_cast<FnOpenDevice *>(m_lib.resolve("OpenDevice"));
    m_CloseDevice = reinterpret_cast<FnCloseDevice *>(m_lib.resolve("CloseDevice"));
    m_InitCAN = reinterpret_cast<FnInitCAN *>(m_lib.resolve("InitCAN"));
    m_StartCAN = reinterpret_cast<FnStartCAN *>(m_lib.resolve("StartCAN"));
    m_ResetCAN = reinterpret_cast<FnResetCAN *>(m_lib.resolve("ResetCAN"));
    m_Transmit = reinterpret_cast<FnTransmit *>(m_lib.resolve("Transmit"));
    m_Receive = reinterpret_cast<FnReceive *>(m_lib.resolve("Receive"));
    m_GetReceiveNum = reinterpret_cast<FnGetReceiveNum *>(m_lib.resolve("GetReceiveNum"));
    m_ClearBuffer = reinterpret_cast<FnClearBuffer *>(m_lib.resolve("ClearBuffer"));
    m_ReadErrInfo = reinterpret_cast<FnReadErrInfo *>(m_lib.resolve("ReadErrInfo"));
    m_ReadCANStatus = reinterpret_cast<FnReadCANStatus *>(m_lib.resolve("ReadCANStatus"));
    m_ReadBoardInfo = reinterpret_cast<FnReadBoardInfo *>(m_lib.resolve("ReadBoardInfo"));

    return m_OpenDevice && m_CloseDevice && m_InitCAN && m_StartCAN && m_Transmit && m_Receive;
}

// 格式化板卡信息。
QString CanWorker::formatBoardInfo(const BOARD_INFO &info) const
{
    const QString serial = QString::fromLocal8Bit(info.str_Serial_Num).trimmed();
    const QString hwType = QString::fromLocal8Bit(info.str_hw_Type).trimmed();
    return QStringLiteral("硬件版本:%1 固件版本:%2 驱动版本:%3 CAN路数:%4 序列号:%5 类型:%6")
        .arg(info.hw_Version)
        .arg(info.fw_Version)
        .arg(info.dr_Version)
        .arg(int(info.can_Num))
        .arg(serial)
        .arg(hwType);
}

// 组合帧类型文本。
QString CanWorker::frameTypeString(bool ext, bool rtr) const
{
    const QString fmt = ext ? QStringLiteral("扩展") : QStringLiteral("标准");
    const QString kind = rtr ? QStringLiteral("远程") : QStringLiteral("数据");
    return fmt + kind + QStringLiteral("帧");
}

// 请求停止线程。
void CanWorker::stopWorker()
{
    m_stopped = true;
}

// 打开设备并初始化通道。
bool CanWorker::openDevice(int deviceType, int deviceIndex, int canIndex, int baudIndex, int mode)
{
    if (!m_OpenDevice) {
        emit openFailed(QStringLiteral("动态库未加载或缺少导出函数"));
        return false;
    }

    m_devtype = deviceType;
    m_devind = deviceIndex;
    m_canind = canIndex;
    m_res = 0;

    if (m_OpenDevice(DWORD(m_devtype), DWORD(m_devind), DWORD(m_res)) != STATUS_OK) {
        ERR_INFO ei = {};
        if (m_ReadErrInfo)
            m_ReadErrInfo(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), &ei);
        emit openFailed(QStringLiteral("OpenDevice 失败 (错误码 0x%1)").arg(ei.ErrCode, 0, 16));
        return false;
    }

    INIT_CONFIG cfg = {};
    CanVci::fillInitConfig(&cfg, baudIndex, mode);
    QThread::msleep(50);

    if (m_InitCAN(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), &cfg) != STATUS_OK) {
        emit openFailed(QStringLiteral("InitCAN 失败"));
        m_CloseDevice(DWORD(m_devtype), DWORD(m_devind));
        return false;
    }

    if (m_StartCAN(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind)) != STATUS_OK) {
        emit openFailed(QStringLiteral("StartCAN 失败"));
        m_CloseDevice(DWORD(m_devtype), DWORD(m_devind));
        return false;
    }

    if (m_ReadBoardInfo) {
        BOARD_INFO bi = {};
        if (m_ReadBoardInfo(DWORD(m_devtype), DWORD(m_devind), &bi) == STATUS_OK)
            emit deviceOpened(formatBoardInfo(bi));
    } else {
        emit deviceOpened(QStringLiteral("已连接"));
    }

    m_stopped = false;
    m_recvRunning = true;
    m_opened = true;
    m_ioTimer.start();
    return true;
}

// 关闭设备。
void CanWorker::closeDevice()
{
    if (!m_opened)
        return;

    m_recvRunning = false;
    if (m_CloseDevice)
        m_CloseDevice(DWORD(m_devtype), DWORD(m_devind));
    m_opened = false;
    emit deviceClosed();
}

// 写入软件 FIFO。
void CanWorker::enqueueRxFifo(quint32 id, bool ext, const QByteArray &data)
{
    QMutexLocker lock(&m_rxFifoMutex);
    while (m_rxFifo.size() >= kRxFifoMax)
        m_rxFifo.dequeue();
    CanRxFifoEntry e;
    e.id = id;
    e.ext = ext;
    e.data = data;
    m_rxFifo.enqueue(e);
}

// 清空软件 FIFO。
void CanWorker::clearRxFifo()
{
    QMutexLocker lock(&m_rxFifoMutex);
    m_rxFifo.clear();
}

void CanWorker::pushSilentRxId(quint32 respId, bool respExt)
{
    QMutexLocker lock(&m_rxSilenceMutex);
    const quint64 key = rxSilenceKey(respId, respExt);
    m_rxSilenceRefs.insert(key, m_rxSilenceRefs.value(key, 0) + 1);
}

void CanWorker::popSilentRxId(quint32 respId, bool respExt)
{
    QMutexLocker lock(&m_rxSilenceMutex);
    const quint64 key = rxSilenceKey(respId, respExt);
    const int refs = m_rxSilenceRefs.value(key, 0);
    if (refs <= 1)
        m_rxSilenceRefs.remove(key);
    else
        m_rxSilenceRefs.insert(key, refs - 1);
}

// 等待指定 ID 的接收帧。
bool CanWorker::waitForRx(quint32 respId, bool respExt, QByteArray *payloadOut, int timeoutMs)
{
    if (!payloadOut)
        return false;

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QMutexLocker lock(&m_rxFifoMutex);
        for (int i = 0; i < m_rxFifo.size(); ++i) {
            const CanRxFifoEntry &e = m_rxFifo.at(i);
            if (e.id == respId && e.ext == respExt) {
                *payloadOut = e.data;
                m_rxFifo.removeAt(i);
                return true;
            }
        }
        lock.unlock();
        QThread::msleep(2);
    }
    return false;
}

// 等待 ISO-TP 流控帧。
bool CanWorker::waitForIsoTpFlowControl(quint32 respId, bool respExt, QByteArray *fcOut, int timeoutMs)
{
    if (!fcOut)
        return false;

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        {
            QMutexLocker lock(&m_rxFifoMutex);
            for (int i = 0; i < m_rxFifo.size(); ++i) {
                const CanRxFifoEntry &e = m_rxFifo.at(i);
                if (e.id != respId || e.ext != respExt || e.data.isEmpty())
                    continue;

                const unsigned pciType = (unsigned(quint8(e.data[0])) >> 4) & 0x0Fu;
                if (pciType != 3u) {
                    m_rxFifo.removeAt(i);
                    lock.unlock();
                    goto drain_continue;
                }

                const unsigned fs = unsigned(quint8(e.data[0])) & 0x0Fu;
                if (fs == 1u) {
                    m_rxFifo.removeAt(i);
                    lock.unlock();
                    goto drain_continue;
                }
                if (fs == 2u) {
                    m_rxFifo.removeAt(i);
                    return false;
                }
                if (fs != 0u) {
                    m_rxFifo.removeAt(i);
                    return false;
                }

                *fcOut = e.data;
                m_rxFifo.removeAt(i);
                return true;
            }
        }
drain_continue:
        QThread::msleep(2);
    }

    return false;
}

// 发送一帧 CAN 报文。
void CanWorker::transmitFrame(quint32 id, const QByteArray &data, bool ext, bool rtr, bool reportToUi)
{
    QMutexLocker txLock(&m_txMutex);
    if (!m_Transmit || !m_opened)
        return;

    CAN_OBJ frame = {};
    frame.ID = id;
    frame.SendType = 0;
    frame.RemoteFlag = rtr ? BYTE(1) : BYTE(0);
    frame.ExternFlag = ext ? BYTE(1) : BYTE(0);

    int dlc = qMin(8, data.size());
    if (rtr && dlc == 0)
        dlc = 8;
    frame.DataLen = BYTE(dlc);
    for (int i = 0; i < dlc; ++i)
        frame.Data[i] = BYTE(data[i]);

    const ULONG sent = m_Transmit(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), &frame, 1);
    if (sent > 0 && reportToUi) {
        const QString t = m_ioTimer.isValid() ? QString::number(m_ioTimer.elapsed()) : QStringLiteral("0");
        const QString idh = formatCanId(frame.ID, ext);
        const QString ft = frameTypeString(ext, rtr);
        const QString dlcStr = QString::number(frame.DataLen);
        const QString dh = rtr ? QString() : toHexSpaced(frame.Data, frame.DataLen);
        emit frameReceived(t, QStringLiteral("TX"), idh, ft, dlcStr, dh);
    }
}

// 清空硬件接收缓冲。
void CanWorker::clearRxBuffer()
{
    if (m_ClearBuffer && m_opened)
        m_ClearBuffer(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind));
}

// 读取总线状态文本。
QString CanWorker::busStatusLine() const
{
    if (!m_ReadCANStatus || !m_opened)
        return QString();

    CAN_STATUS st = {};
    if (m_ReadCANStatus(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), &st) != STATUS_OK)
        return QString();

    return QStringLiteral("Mode:%1 Status:0x%2 TEC:%3 REC:%4")
        .arg(st.regMode)
        .arg(st.regStatus, 2, 16, QChar('0'))
        .arg(st.regTECounter)
        .arg(st.regRECounter);
}

bool CanWorker::isRxUiSilenced(quint32 id, bool ext) const
{
    QMutexLocker lock(&m_rxSilenceMutex);
    return m_rxSilenceRefs.value(rxSilenceKey(id, ext), 0) > 0;
}

quint64 CanWorker::rxSilenceKey(quint32 id, bool ext)
{
    return (quint64(ext ? 1u : 0u) << 32) | quint64(id);
}

// 线程主循环：轮询接收并发射 frameReceived 信号。
void CanWorker::run()
{
    CAN_OBJ buf[64];

    while (!m_stopped) {
        if (!m_Receive || !m_recvRunning || !m_opened) {
            QThread::msleep(5);
            continue;
        }

        const ULONG n = m_Receive(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), buf, 64, 10);
        if (n == ULONG(-1) || n == 0xFFFFFFFFu) {
            ERR_INFO ei = {};
            if (m_ReadErrInfo && m_ReadErrInfo(DWORD(m_devtype), DWORD(m_devind), DWORD(m_canind), &ei) == STATUS_OK
                && ei.ErrCode != 0) {
                qDebug() << "ReadErrInfo 0x" << QString::number(ei.ErrCode, 16);
            }
            QThread::msleep(2);
            continue;
        }

        for (ULONG i = 0; i < n; ++i) {
            const CAN_OBJ &f = buf[i];
            const bool ext = f.ExternFlag != 0;
            const bool rtr = f.RemoteFlag != 0;
            const QString t = m_ioTimer.isValid() ? QString::number(m_ioTimer.elapsed()) : QStringLiteral("0");
            const QString idh = formatCanId(f.ID, ext);
            const QString ft = frameTypeString(ext, rtr);
            const QString dlc = QString::number(f.DataLen);
            const QString dh = rtr ? QString() : toHexSpaced(f.Data, f.DataLen);

            if (!rtr && f.DataLen > 0) {
                QByteArray raw(int(f.DataLen), Qt::Uninitialized);
                for (int b = 0; b < int(f.DataLen); ++b)
                    raw[b] = char(f.Data[b]);
                enqueueRxFifo(quint32(f.ID), ext, raw);
            }

            if (!isRxUiSilenced(quint32(f.ID), ext))
                emit frameReceived(t, QStringLiteral("RX"), idh, ft, dlc, dh);
        }

        QThread::msleep(1);
    }
}
