#ifndef CAN_WORKER_API_H
#define CAN_WORKER_API_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QLibrary>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QThread>

#include "ECanVci.h"

// CAN 接收 FIFO 条目，仅保存协议解析所需的最小信息。
struct CanRxFifoEntry {
    quint32 id = 0;
    bool ext = false;
    QByteArray data;
};

namespace CanVci {
void fillInitConfig(INIT_CONFIG *cfg, int baudIndex, int mode);
QString baudIndexToLabel(int baudIndex);
int baudLabelCount();
}

class CanWorker : public QThread
{
    Q_OBJECT
public:
    explicit CanWorker(QObject *parent = nullptr);
    ~CanWorker() override;

    void stopWorker();
    bool openDevice(int deviceType, int deviceIndex, int canIndex, int baudIndex, int mode);
    void closeDevice();

    // reportToUi=false 时不把该 TX 记入 CAN 收发模块日志。
    void transmitFrame(quint32 id, const QByteArray &data, bool ext, bool rtr, bool reportToUi = true);
    void clearRxBuffer();
    void clearRxFifo();
    bool waitForRx(quint32 respId, bool respExt, QByteArray *payloadOut, int timeoutMs);
    bool waitForIsoTpFlowControl(quint32 respId, bool respExt, QByteArray *fcOut, int timeoutMs);
    void pushSilentRxId(quint32 respId, bool respExt);
    void popSilentRxId(quint32 respId, bool respExt);

    QString busStatusLine() const;

    bool isDeviceOpen() const { return m_opened; }
    int deviceType() const { return m_devtype; }
    int deviceIndex() const { return m_devind; }
    int canIndex() const { return m_canind; }

signals:
    void frameReceived(const QString &timeMs, const QString &dir, const QString &idHex,
                       const QString &frameType, const QString &dlc, const QString &dataHex);
    void deviceOpened(const QString &boardInfoLine);
    void openFailed(const QString &reason);
    void deviceClosed();

protected:
    void run() override;

private:
    bool resolveApi();
    QString formatBoardInfo(const BOARD_INFO &info) const;
    QString frameTypeString(bool ext, bool rtr) const;
    bool isRxUiSilenced(quint32 id, bool ext) const;
    static quint64 rxSilenceKey(quint32 id, bool ext);
    void enqueueRxFifo(quint32 id, bool ext, const QByteArray &data);

    volatile bool m_stopped = false;
    volatile bool m_recvRunning = false;
    bool m_opened = false;
    int m_devtype = USBCAN1;
    int m_devind = 0;
    int m_res = 0;
    int m_canind = 0;

    typedef DWORD(__stdcall FnOpenDevice)(DWORD, DWORD, DWORD);
    typedef DWORD(__stdcall FnCloseDevice)(DWORD, DWORD);
    typedef DWORD(__stdcall FnInitCAN)(DWORD, DWORD, DWORD, P_INIT_CONFIG);
    typedef DWORD(__stdcall FnStartCAN)(DWORD, DWORD, DWORD);
    typedef DWORD(__stdcall FnResetCAN)(DWORD, DWORD, DWORD);
    typedef ULONG(__stdcall FnTransmit)(DWORD, DWORD, DWORD, P_CAN_OBJ, ULONG);
    typedef ULONG(__stdcall FnReceive)(DWORD, DWORD, DWORD, P_CAN_OBJ, ULONG, INT);
    typedef ULONG(__stdcall FnGetReceiveNum)(DWORD, DWORD, DWORD);
    typedef DWORD(__stdcall FnClearBuffer)(DWORD, DWORD, DWORD);
    typedef DWORD(__stdcall FnReadErrInfo)(DWORD, DWORD, DWORD, P_ERR_INFO);
    typedef DWORD(__stdcall FnReadCANStatus)(DWORD, DWORD, DWORD, P_CAN_STATUS);
    typedef DWORD(__stdcall FnReadBoardInfo)(DWORD, DWORD, P_BOARD_INFO);

    FnOpenDevice *m_OpenDevice = nullptr;
    FnCloseDevice *m_CloseDevice = nullptr;
    FnInitCAN *m_InitCAN = nullptr;
    FnStartCAN *m_StartCAN = nullptr;
    FnResetCAN *m_ResetCAN = nullptr;
    FnTransmit *m_Transmit = nullptr;
    FnReceive *m_Receive = nullptr;
    FnGetReceiveNum *m_GetReceiveNum = nullptr;
    FnClearBuffer *m_ClearBuffer = nullptr;
    FnReadErrInfo *m_ReadErrInfo = nullptr;
    FnReadCANStatus *m_ReadCANStatus = nullptr;
    FnReadBoardInfo *m_ReadBoardInfo = nullptr;

    QLibrary m_lib;
    QMutex m_txMutex;
    QMutex m_rxFifoMutex;
    QQueue<CanRxFifoEntry> m_rxFifo;
    mutable QMutex m_rxSilenceMutex;
    QHash<quint64, int> m_rxSilenceRefs;
    static const int kRxFifoMax = 2048;
    QElapsedTimer m_ioTimer;
};

#endif // CAN_WORKER_API_H
