#ifndef UDS_FLASH_WORKER_H
#define UDS_FLASH_WORKER_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QThread>

class CanWorker;

// 文件说明：UDS 刷写执行线程，负责按流程执行会话/下载/传输/退出等步骤。

struct UdsFlashStep
{
    // 刷写步骤类型。
    enum StepType {
        StepRawRequest = 0,
        StepDelay,
        StepSessionControlAuto,
        StepRoutineEraseAuto,
        StepRoutineCrcAuto,
        StepRequestDownloadAuto,
        StepTransferDataAuto,
        StepTransferExitAuto,
        StepEcuResetAuto
    };

    bool enabled = true;              // 是否启用该步骤
    QString name;                     // 步骤显示名称
    StepType type = StepRawRequest;   // 步骤执行类型
    QByteArray request;               // RAW 步骤请求字节
    int timeoutMs = 3000;             // 步骤超时（ms）
    int retries = 0;                  // 失败重试次数
    bool checkPositiveSid = false;    // 是否校验正响应 SID
    quint8 expectedPositiveSid = 0;   // 期望正响应 SID 值
};

class UdsFlashWorker : public QThread
{
    Q_OBJECT
public:
    // 构造刷写线程。
    explicit UdsFlashWorker(QObject *parent = nullptr);

    // 配置刷写参数与步骤（启动前调用）。
    void configure(CanWorker *worker,
                   quint32 txId,
                   quint32 rxId,
                   bool ext,
                   const QString &filePath,
                   quint32 flashStartAddress,
                   int maxPayloadPerTransferData,
                   int isoTpTimeoutMs,
                   bool testerPresentDuringFlash,
                   const QList<UdsFlashStep> &steps,
                   bool requireFirmwareImage = true);

    // 请求终止当前刷写流程。
    void requestAbort();

signals:
    // 日志输出。
    void logLine(const QString &line);
    // 进度回调（0~100）。
    void progressValue(int percent);
    // 刷写成功。
    void finishedOk(const QString &summary);
    // 刷写失败。
    void finishedError(const QString &reason);
    void transferDataBlockStarted(int block1Based, int totalBlocks); // TransferData 块开始
    void flashIsoTpTxFrame(const QString &stepName, int frameIndex1Based); // ISO-TP 发帧通知

protected:
    // 线程主函数。
    void run() override;

private:
    bool udsExchange(const QByteArray &req, QByteArray *resp, const QString &stepName, int timeoutMs); // 单步 UDS 交换
    bool executeRawStep(const UdsFlashStep &step); // 执行 RAW 步骤
    bool executeDelayStep(const UdsFlashStep &step); // 执行延时步骤
    bool executeSessionControlStep(const UdsFlashStep &step); // 执行会话控制
    bool executeRoutineEraseStep(const UdsFlashStep &step, const QByteArray &fw); // 执行擦除
    bool executeRoutineCrcStep(const UdsFlashStep &step, const QByteArray &fw); // 执行 CRC 校验
    bool executeRequestDownloadStep(const UdsFlashStep &step, const QByteArray &fw, int *chunkPayloadOut); // 执行 34
    bool executeTransferDataStep(const UdsFlashStep &step, const QByteArray &fw, int chunkPayload); // 执行 36
    bool executeTransferExitStep(const UdsFlashStep &step); // 执行 37
    bool executeEcuResetStep(const UdsFlashStep &step); // 执行 11

    static bool isNegative(const QByteArray &r, quint8 *nrcOut); // 判定否定响应
    static QString nrcText(quint8 nrc); // NRC 解释文本
    static int parseMaxBlockLengthFrom34Response(const QByteArray &resp); // 解析块长
    static quint32 calcCrc32(const QByteArray &data); // 计算 CRC32(IEEE)
    static QByteArray loadIntelHex(const QString &path, quint32 flashStart, QString *err, QString *warnRelocated = nullptr); // 读取 HEX

    CanWorker *m_worker = nullptr; // 底层 CAN worker
    quint32 m_txId = 0; // 请求 ID
    quint32 m_rxId = 0; // 响应 ID
    bool m_ext = false; // 扩展帧标记

    QString m_filePath; // 固件路径
    quint32 m_startAddr = 0; // 刷写起始地址
    int m_maxPayload = 512; // 每块最大数据
    int m_isoTimeoutMs = 3000; // 协议超时
    bool m_testerPresent = true; // 周期保活开关
    QList<UdsFlashStep> m_steps; // 执行步骤
    bool m_requireFirmwareImage = true; // 是否要求固件文件

    volatile bool m_abort = false; // 中止标记
};

#endif
