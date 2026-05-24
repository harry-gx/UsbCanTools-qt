#include "uds_flash_worker.h"

#include "can/can_worker_api.h"
#include "isotp.h"

// 说明：本文件为 UDS 刷写执行线程实现，负责按步骤执行刷写流程。

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>
#include <QThread>
#include <QtGlobal>

namespace {

// 将字节流转换为大写空格分隔 HEX 文本。
QString toSpacedHex(const QByteArray &data)
{
    if (data.isEmpty())
        return QString();
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

} // namespace

UdsFlashWorker::UdsFlashWorker(QObject *parent)
    : QThread(parent)
{
    m_abort = false;
}

// 配置线程执行参数（启动前调用）。
void UdsFlashWorker::configure(CanWorker *worker,
                               quint32 txId,
                               quint32 rxId,
                               bool ext,
                               const QString &filePath,
                               quint32 flashStartAddress,
                               int maxPayloadPerTransferData,
                               int isoTpTimeoutMs,
                               bool testerPresentDuringFlash,
                               const QList<UdsFlashStep> &steps,
                               bool requireFirmwareImage)
{
    m_worker = worker;
    m_txId = txId;
    m_rxId = rxId;
    m_ext = ext;
    m_filePath = filePath;
    m_startAddr = flashStartAddress;
    m_maxPayload = qBound(1, maxPayloadPerTransferData, 4090);
    m_isoTimeoutMs = qMax(500, isoTpTimeoutMs);
    m_testerPresent = testerPresentDuringFlash;
    m_steps = steps;
    m_requireFirmwareImage = requireFirmwareImage;
}

// 请求中止当前刷写流程。
void UdsFlashWorker::requestAbort()
{
    m_abort = true;
}

// 判断响应是否为否定响应（0x7F）。
bool UdsFlashWorker::isNegative(const QByteArray &r, quint8 *nrcOut)
{
    if (r.size() >= 3 && quint8(r[0]) == 0x7Fu) {
        if (nrcOut)
            *nrcOut = quint8(r[2]);
        return true;
    }
    return false;
}

// 将 NRC 码转换为可读文本。
QString UdsFlashWorker::nrcText(quint8 nrc)
{
    switch (nrc) {
    case 0x10: return QStringLiteral("generalReject");
    case 0x11: return QStringLiteral("serviceNotSupported");
    case 0x12: return QStringLiteral("subFunctionNotSupported");
    case 0x13: return QStringLiteral("incorrectMessageLengthOrInvalidFormat");
    case 0x22: return QStringLiteral("conditionsNotCorrect");
    case 0x24: return QStringLiteral("requestSequenceError");
    case 0x31: return QStringLiteral("requestOutOfRange");
    case 0x33: return QStringLiteral("securityAccessDenied");
    case 0x35: return QStringLiteral("invalidKey");
    case 0x36: return QStringLiteral("exceedNumberOfAttempts");
    case 0x37: return QStringLiteral("requiredTimeDelayNotExpired");
    case 0x70: return QStringLiteral("uploadDownloadNotAccepted");
    case 0x71: return QStringLiteral("transferDataSuspended");
    case 0x72: return QStringLiteral("generalProgrammingFailure");
    case 0x73: return QStringLiteral("wrongBlockSequenceCounter");
    case 0x78: return QStringLiteral("responsePending");
    default: return QStringLiteral("NRC 0x%1").arg(nrc, 2, 16, QChar('0')).toUpper();
    }
}

// 解析 0x34 正响应中的最大块长（maxNumberOfBlockLength）。
int UdsFlashWorker::parseMaxBlockLengthFrom34Response(const QByteArray &resp)
{
    if (resp.size() < 4 || quint8(resp[0]) != 0x74)
        return -1;

    const quint8 lfi = quint8(resp[1]);
    quint32 maxBlock = 0;
    const int bytes = int((lfi >> 4) & 0x0F);

    if (bytes >= 1 && bytes <= 4 && resp.size() >= 2 + bytes) {
        for (int i = 0; i < bytes; ++i)
            maxBlock = (maxBlock << 8) | quint32(quint8(resp[2 + i]));
    } else if (lfi == 0x02u && resp.size() >= 4) {
        maxBlock = (quint32(quint8(resp[2])) << 8) | quint32(quint8(resp[3]));
    } else {
        return -1;
    }

    if (maxBlock < 3)
        return -1;
    return int(maxBlock);
}

quint32 UdsFlashWorker::calcCrc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= quint32(quint8(data.at(i)));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

// 读取 Intel HEX 文件并转换为连续内存镜像。
QByteArray UdsFlashWorker::loadIntelHex(const QString &path, quint32 flashStart, QString *err, QString *warnRelocated)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("无法打开 HEX 文件");
        return {};
    }

    QMap<quint32, quint8> mem;
    quint32 upper = 0;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1Char(':')))
            continue;

        line = line.mid(1);
        if (line.size() < 8)
            continue;

        bool ok = false;
        QByteArray raw;
        raw.reserve(line.size() / 2);
        for (int i = 0; i + 1 < line.size(); i += 2) {
            const int b = line.mid(i, 2).toUInt(&ok, 16);
            if (!ok) {
                raw.clear();
                break;
            }
            raw.append(char(b));
        }
        if (raw.size() < 5)
            continue;

        const int reclen = quint8(raw[0]);
        if (raw.size() < 4 + reclen)
            continue;

        const quint32 addr16 = (quint32(quint8(raw[1])) << 8) | quint32(quint8(raw[2]));
        const int rectype = int(quint8(raw[3]));

        if (rectype == 0) {
            const quint32 base = upper + addr16;
            for (int i = 0; i < reclen; ++i)
                mem.insert(base + quint32(i), quint8(raw[4 + i]));
        } else if (rectype == 1) {
            break;
        } else if (rectype == 4) {
            if (reclen >= 2)
                upper = ((quint32(quint8(raw[4])) << 8) | quint32(quint8(raw[5]))) << 16;
        }
    }
    f.close();

    if (mem.isEmpty()) {
        if (err)
            *err = QStringLiteral("HEX 中没有有效数据");
        return {};
    }

    quint32 lo = mem.firstKey();
    quint32 hi = lo;
    for (auto it = mem.constBegin(); it != mem.constEnd(); ++it)
        hi = qMax(hi, it.key());

    QByteArray out;
    if (flashStart > hi) {
        const int n = int(hi - lo + 1);
        out.resize(n);
        out.fill(char(0xFF));
        for (auto it = mem.constBegin(); it != mem.constEnd(); ++it) {
            const int off = int(it.key() - lo);
            if (off >= 0 && off < n)
                out[off] = char(it.value());
        }

        if (warnRelocated) {
            *warnRelocated = QStringLiteral("HEX 最高地址 0x%1 小于下载地址 0x%2，已按连续镜像发送")
                                .arg(hi, 8, 16, QChar('0')).toUpper()
                                .arg(flashStart, 8, 16, QChar('0')).toUpper();
        }
    } else {
        const int n = int(hi - flashStart + 1);
        out.resize(n);
        out.fill(char(0xFF));
        for (auto it = mem.constBegin(); it != mem.constEnd(); ++it) {
            if (it.key() < flashStart)
                continue;
            const int off = int(it.key() - flashStart);
            if (off >= 0 && off < n)
                out[off] = char(it.value());
        }
    }

    if (err)
        err->clear();
    return out;
}

// 执行一次 UDS 请求响应交换（含 0x78 pending 处理）。
bool UdsFlashWorker::udsExchange(const QByteArray &req, QByteArray *resp, const QString &stepName, int timeoutMs)
{
    if (!m_worker)
        return false;

    emit logLine(QStringLiteral("[%1] TX: %2").arg(stepName, toSpacedHex(req)));
    m_worker->pushSilentRxId(m_rxId, m_ext);

    QByteArray r;
    const IsoTp::TxFrameProgressCallback onTx = [this, stepName](int frameIdx) {
        emit flashIsoTpTxFrame(stepName, frameIdx);
    };

    const bool transportOk = IsoTp::sendReceive(m_worker, m_txId, m_rxId, m_ext, req, &r, timeoutMs, onTx);
    if (!transportOk) {
        m_worker->popSilentRxId(m_rxId, m_ext);
        emit logLine(QStringLiteral("[%1] ISO-TP 收发失败").arg(stepName));
        return false;
    }

    int nPending = 0;
    while (true) {
        quint8 nrc = 0;
        if (!isNegative(r, &nrc))
            break;

        if (nrc != 0x78) {
            m_worker->popSilentRxId(m_rxId, m_ext);
            emit logLine(QStringLiteral("[%1] RX: %2").arg(stepName, toSpacedHex(r)));
            emit logLine(QStringLiteral("[%1] 否定响应: %2").arg(stepName).arg(nrcText(nrc)));
            return false;
        }

        if (++nPending > 50) {
            m_worker->popSilentRxId(m_rxId, m_ext);
            emit logLine(QStringLiteral("[%1] 0x78 过多").arg(stepName));
            return false;
        }

        if (!IsoTp::receivePdu(m_worker, m_txId, m_rxId, m_ext, &r, timeoutMs)) {
            m_worker->popSilentRxId(m_rxId, m_ext);
            emit logLine(QStringLiteral("[%1] 等待0x78后续响应超时").arg(stepName));
            return false;
        }
        emit logLine(QStringLiteral("[%1] RX(Pending后): %2").arg(stepName, toSpacedHex(r)));
    }

    m_worker->popSilentRxId(m_rxId, m_ext);
    emit logLine(QStringLiteral("[%1] RX: %2").arg(stepName, toSpacedHex(r)));

    if (resp)
        *resp = r;
    return true;
}

// 执行 RAW 类型步骤。
bool UdsFlashWorker::executeRawStep(const UdsFlashStep &step)
{
    QByteArray resp;
    const int timeout = step.timeoutMs > 0 ? step.timeoutMs : m_isoTimeoutMs;

    bool ok = false;
    for (int i = 0; i <= qMax(0, step.retries); ++i) {
        if (i > 0)
            emit logLine(QStringLiteral("[%1] 重试 %2").arg(step.name).arg(i));

        if (!udsExchange(step.request, &resp, step.name, timeout))
            continue;

        if (step.checkPositiveSid) {
            if (resp.isEmpty() || quint8(resp[0]) != step.expectedPositiveSid) {
                emit logLine(QStringLiteral("[%1] 正响应SID不匹配, expect=0x%2 recv=0x%3")
                                 .arg(step.name)
                                 .arg(step.expectedPositiveSid, 2, 16, QChar('0')).toUpper()
                                 .arg(resp.isEmpty() ? QStringLiteral("--")
                                                     : QStringLiteral("%1").arg(quint8(resp[0]), 2, 16, QChar('0')).toUpper()));
                continue;
            }
        }

        ok = true;
        break;
    }

    return ok;
}

// 执行延时步骤，支持在等待期间响应中止请求。
bool UdsFlashWorker::executeDelayStep(const UdsFlashStep &step)
{
    const int delayMs = qMax(0, step.timeoutMs);
    emit logLine(QStringLiteral("[%1] 延时 %2 ms").arg(step.name).arg(delayMs));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < delayMs) {
        if (m_abort)
            return false;
        const int remain = delayMs - int(timer.elapsed());
        QThread::msleep(static_cast<unsigned long>(qMin(remain, 50)));
    }
    return true;
}

// 执行自动会话控制（10 02）。
bool UdsFlashWorker::executeSessionControlStep(const UdsFlashStep &step)
{
    QByteArray req;
    req.append(char(0x10));
    req.append(char(0x02));

    UdsFlashStep s = step;
    s.request = req;
    if (!s.checkPositiveSid) {
        s.checkPositiveSid = true;
        s.expectedPositiveSid = 0x50;
    }
    return executeRawStep(s);
}

// 执行自动擦除例程（31 01 FF00 + 地址长度）。
bool UdsFlashWorker::executeRoutineEraseStep(const UdsFlashStep &step, const QByteArray &fw)
{
    QByteArray req;
    req.append(char(0x31));
    req.append(char(0x01));
    req.append(char(0xFF));
    req.append(char(0x00));

    const quint32 addr = m_startAddr;
    const quint32 sz = quint32(fw.size());
    req.append(char((addr >> 24) & 0xFF));
    req.append(char((addr >> 16) & 0xFF));
    req.append(char((addr >> 8) & 0xFF));
    req.append(char(addr & 0xFF));
    req.append(char((sz >> 24) & 0xFF));
    req.append(char((sz >> 16) & 0xFF));
    req.append(char((sz >> 8) & 0xFF));
    req.append(char(sz & 0xFF));

    UdsFlashStep s = step;
    s.request = req;
    if (!s.checkPositiveSid) {
        s.checkPositiveSid = true;
        s.expectedPositiveSid = 0x71;
    }
    return executeRawStep(s);
}

bool UdsFlashWorker::executeRoutineCrcStep(const UdsFlashStep &step, const QByteArray &fw)
{
    const quint32 sz = quint32(fw.size());
    const quint32 crc = calcCrc32(fw);

    QByteArray req;
    req.append(char(0x31));
    req.append(char(0x01));
    req.append(char(0xFF));
    req.append(char(0x01));
    req.append(char((sz >> 24) & 0xFF));
    req.append(char((sz >> 16) & 0xFF));
    req.append(char((sz >> 8) & 0xFF));
    req.append(char(sz & 0xFF));
    req.append(char((crc >> 24) & 0xFF));
    req.append(char((crc >> 16) & 0xFF));
    req.append(char((crc >> 8) & 0xFF));
    req.append(char(crc & 0xFF));

    emit logLine(QStringLiteral("[%1] CRC32(IEEE)=0x%2, 长度=%3 字节")
                     .arg(step.name)
                     .arg(crc, 8, 16, QChar('0')).toUpper()
                     .arg(sz));

    UdsFlashStep s = step;
    s.request = req;
    if (!s.checkPositiveSid) {
        s.checkPositiveSid = true;
        s.expectedPositiveSid = 0x71;
    }
    return executeRawStep(s);
}

// 执行自动下载请求（34）并计算有效块载荷大小。
bool UdsFlashWorker::executeRequestDownloadStep(const UdsFlashStep &step, const QByteArray &fw, int *chunkPayloadOut)
{
    QByteArray req;
    req.append(char(0x34));
    req.append(char(0x00));
    req.append(char(0x44));

    const quint32 addr = m_startAddr;
    const quint32 sz = quint32(fw.size());
    req.append(char((addr >> 24) & 0xFF));
    req.append(char((addr >> 16) & 0xFF));
    req.append(char((addr >> 8) & 0xFF));
    req.append(char(addr & 0xFF));
    req.append(char((sz >> 24) & 0xFF));
    req.append(char((sz >> 16) & 0xFF));
    req.append(char((sz >> 8) & 0xFF));
    req.append(char(sz & 0xFF));

    const int timeout = step.timeoutMs > 0 ? step.timeoutMs : qMax(3000, m_isoTimeoutMs);
    QByteArray resp;

    bool ok = false;
    for (int i = 0; i <= qMax(0, step.retries); ++i) {
        if (i > 0) {
            emit logLine(QStringLiteral("[%1] 重试 %2").arg(step.name).arg(i));
            m_worker->clearRxFifo();
            QThread::msleep(20);
        }
        ok = udsExchange(req, &resp, step.name, timeout);
        if (ok)
            break;
    }
    if (!ok)
        return false;

    if (resp.isEmpty() || quint8(resp[0]) != 0x74) {
        emit logLine(QStringLiteral("[%1] 响应SID异常").arg(step.name));
        return false;
    }

    int chunkPayload = 512;
    const int maxBlk = parseMaxBlockLengthFrom34Response(resp);
    if (maxBlk > 2)
        chunkPayload = maxBlk - 2;

    chunkPayload = qBound(1, chunkPayload, 4090);
    if (m_maxPayload < chunkPayload) {
        emit logLine(QStringLiteral("[%1] 每块最大数据过小，至少需要%2字节").arg(step.name).arg(chunkPayload));
        return false;
    }

    if (chunkPayloadOut)
        *chunkPayloadOut = chunkPayload;

    emit logLine(QStringLiteral("RequestDownload OK, chunk=%1").arg(chunkPayload));
    return true;
}

// 执行自动数据传输（36），按块发送固件。
bool UdsFlashWorker::executeTransferDataStep(const UdsFlashStep &step, const QByteArray &fw, int chunkPayload)
{
    const int timeout = step.timeoutMs > 0 ? step.timeoutMs : m_isoTimeoutMs;

    quint8 blockSeq = 1;
    qint64 done = 0;
    const int total = fw.size();

    QElapsedTimer tpTimer;
    tpTimer.start();

    while (done < total) {
        if (m_abort)
            return false;

        if (m_testerPresent && tpTimer.elapsed() >= 2000) {
            tpTimer.restart();
            QByteArray sf(8, char(0x00));
            sf[0] = char(0x02);
            sf[1] = char(0x3E);
            sf[2] = char(0x80);
            m_worker->transmitFrame(m_txId, sf, m_ext, false, false);
        }

        const int take = qMin(chunkPayload, total - int(done));
        const int totalBlocks = (total + chunkPayload - 1) / chunkPayload;
        const int blockIndex = int(done / chunkPayload) + 1;
        emit transferDataBlockStarted(blockIndex, totalBlocks);

        QByteArray td;
        td.append(char(0x36));
        td.append(char(blockSeq));
        td.append(fw.mid(int(done), take));

        QByteArray resp;
        bool ok = false;
        for (int i = 0; i <= qMax(0, step.retries); ++i) {
            if (i > 0)
                emit logLine(QStringLiteral("[TransferData block %1] 重试 %2").arg(blockIndex).arg(i));

            if (!udsExchange(td, &resp, step.name, timeout))
                continue;

            if (resp.size() < 2 || quint8(resp[0]) != 0x76 || quint8(resp[1]) != blockSeq)
                continue;

            ok = true;
            break;
        }

        if (!ok)
            return false;

        done += take;
        blockSeq = (blockSeq == 0xFF) ? 1 : quint8(blockSeq + 1);
        emit progressValue(int(done * 100 / total));
    }

    return true;
}

// 执行自动传输结束（37）。
bool UdsFlashWorker::executeTransferExitStep(const UdsFlashStep &step)
{
    QByteArray resp;
    const int timeout = qMax(step.timeoutMs > 0 ? step.timeoutMs : m_isoTimeoutMs, 15000);

    // 某些 Bootloader 在收到最后一个 36 块后仍需短暂完成内部落盘，再接收 37。
    msleep(100);

    bool ok = false;
    for (int i = 0; i <= qMax(0, step.retries); ++i) {
        if (i > 0)
            emit logLine(QStringLiteral("[%1] 重试 %2").arg(step.name).arg(i));

        if (!udsExchange(QByteArray::fromHex("37"), &resp, step.name, timeout))
            continue;

        if (resp.isEmpty() || quint8(resp[0]) != 0x77)
            continue;

        ok = true;
        break;
    }

    return ok;
}

// 执行自动 ECU 复位（11 01）。
bool UdsFlashWorker::executeEcuResetStep(const UdsFlashStep &step)
{
    QByteArray req;
    req.append(char(0x11));
    req.append(char(0x01));

    UdsFlashStep s = step;
    s.request = req;
    if (!s.checkPositiveSid) {
        s.checkPositiveSid = true;
        s.expectedPositiveSid = 0x51;
    }
    return executeRawStep(s);
}

// 线程主流程：读取固件并按步骤执行刷写。
void UdsFlashWorker::run()
{
    if (!m_worker || !m_worker->isDeviceOpen()) {
        emit finishedError(QStringLiteral("设备未连接"));
        return;
    }

    QByteArray fw;
    if (m_requireFirmwareImage) {
        const QFileInfo fwInfo(m_filePath);
        if (fwInfo.suffix().compare(QLatin1String("hex"), Qt::CaseInsensitive) == 0) {
            QString err;
            QString warn;
            fw = loadIntelHex(m_filePath, m_startAddr, &err, &warn);
            if (fw.isEmpty()) {
                emit finishedError(err.isEmpty() ? QStringLiteral("HEX 解析失败") : err);
                return;
            }
            if (!warn.isEmpty())
                emit logLine(warn);
        } else {
            QFile f(m_filePath);
            if (!f.open(QIODevice::ReadOnly)) {
                emit finishedError(QStringLiteral("无法打开固件文件"));
                return;
            }
            fw = f.readAll();
            f.close();
            if (fw.isEmpty()) {
                emit finishedError(QStringLiteral("固件为空"));
                return;
            }
        }
    }

    m_worker->clearRxBuffer();
    m_worker->clearRxFifo();
    m_abort = false;
    emit progressValue(0);
    if (m_requireFirmwareImage)
        emit logLine(QStringLiteral("开始刷写，固件大小 %1 字节").arg(fw.size()));
    else
        emit logLine(QStringLiteral("开始执行 UDS 诊断流程"));

    int chunkPayload = qBound(1, m_maxPayload, 4090);
    int enabledCount = 0;

    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_abort) {
            emit finishedError(QStringLiteral("用户中止"));
            return;
        }

        const UdsFlashStep step = m_steps.at(i);
        if (!step.enabled)
            continue;
        ++enabledCount;

        emit logLine(QStringLiteral("执行步骤: %1").arg(step.name));

        bool ok = false;
        switch (step.type) {
        case UdsFlashStep::StepRawRequest:
            ok = executeRawStep(step);
            break;
        case UdsFlashStep::StepDelay:
            ok = executeDelayStep(step);
            break;
        case UdsFlashStep::StepSessionControlAuto:
            ok = executeSessionControlStep(step);
            break;
        case UdsFlashStep::StepRoutineEraseAuto:
            if (!m_requireFirmwareImage) {
                emit logLine(QStringLiteral("[%1] 诊断流程不支持该刷写步骤").arg(step.name));
                ok = false;
                break;
            }
            ok = executeRoutineEraseStep(step, fw);
            break;
        case UdsFlashStep::StepRoutineCrcAuto:
            if (!m_requireFirmwareImage) {
                emit logLine(QStringLiteral("[%1] 诊断流程不支持该刷写步骤").arg(step.name));
                ok = false;
                break;
            }
            ok = executeRoutineCrcStep(step, fw);
            break;
        case UdsFlashStep::StepRequestDownloadAuto:
            if (!m_requireFirmwareImage) {
                emit logLine(QStringLiteral("[%1] 诊断流程不支持该刷写步骤").arg(step.name));
                ok = false;
                break;
            }
            ok = executeRequestDownloadStep(step, fw, &chunkPayload);
            break;
        case UdsFlashStep::StepTransferDataAuto:
            if (!m_requireFirmwareImage) {
                emit logLine(QStringLiteral("[%1] 诊断流程不支持该刷写步骤").arg(step.name));
                ok = false;
                break;
            }
            ok = executeTransferDataStep(step, fw, chunkPayload);
            break;
        case UdsFlashStep::StepTransferExitAuto:
            if (!m_requireFirmwareImage) {
                emit logLine(QStringLiteral("[%1] 诊断流程不支持该刷写步骤").arg(step.name));
                ok = false;
                break;
            }
            ok = executeTransferExitStep(step);
            break;
        case UdsFlashStep::StepEcuResetAuto:
            ok = executeEcuResetStep(step);
            break;
        }

        if (!ok) {
            emit finishedError(QStringLiteral("步骤失败: %1").arg(step.name));
            return;
        }
    }

    emit progressValue(100);
    if (m_requireFirmwareImage)
        emit finishedOk(QStringLiteral("刷写完成，共 %1 字节").arg(fw.size()));
    else
        emit finishedOk(QStringLiteral("UDS 诊断流程执行完成，共 %1 步").arg(enabledCount));
}
