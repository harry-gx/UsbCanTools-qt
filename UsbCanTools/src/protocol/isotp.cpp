// 文件说明：ISO-TP 协议实现，提供 UDS 请求发送与响应重组能力。
#include "protocol/isotp.h"
#include "can/can_worker_api.h"
#include <QElapsedTimer>
#include <QtGlobal>
#include <QThread>

namespace {

// 发送一帧 8 字节 CAN 数据。
void tx(CanWorker *w, quint32 txId, bool ext, const QByteArray &eightBytes)
{
    w->transmitFrame(txId, eightBytes, ext, false, false);
}

// 在超时范围内循环等待一帧响应。
bool waitFr(CanWorker *w, quint32 rxId, bool ext, QByteArray *out, int leftMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < leftMs) {
        const int remain = leftMs - int(t.elapsed());
        if (remain <= 0)
            break;
        if (w->waitForRx(rxId, ext, out, qMax(5, remain)))
            return true;
    }
    return false;
}

// 接收并重组完整的 ISO-TP 响应 PDU。
bool receiveResponsePdu(CanWorker *worker,
                        quint32 txId,
                        quint32 rxId,
                        bool ext,
                        QByteArray *responsePduOut,
                        int timeoutMs)
{
    QByteArray fr;
    if (!waitFr(worker, rxId, ext, &fr, timeoutMs))
        return false;
    if (fr.isEmpty())
        return false;

    const unsigned pci0 = unsigned(fr[0]) & 0xF0u;
    if (pci0 == 0x00) {
        const int sfDl = int(fr[0] & 0x0Fu);
        if (sfDl < 1 || sfDl > 7)
            return false;
        *responsePduOut = fr.mid(1, sfDl);
        return true;
    }

    if (pci0 != 0x10)
        return false;

    const int total = int((unsigned(fr[0] & 0x0Fu) << 8) | unsigned(fr[1] & 0xFFu));
    if (total < 1 || fr.size() < 8)
        return false;
    QByteArray acc = fr.mid(2, 6);
    {
        QByteArray fcout(8, char(0x00));
        fcout[0] = char(0x30);
        fcout[1] = 0;
        fcout[2] = 0;
        tx(worker, txId, ext, fcout);
    }

    quint8 seq = 1;
    QElapsedTimer t;
    t.start();
    while (acc.size() < total) {
        const int left = timeoutMs - int(t.elapsed());
        if (left <= 0)
            return false;
        if (!waitFr(worker, rxId, ext, &fr, left))
            return false;
        if (fr.isEmpty())
            return false;
        if ((unsigned(fr[0]) & 0xF0u) != 0x20u)
            return false;
        if ((fr[0] & 0x0F) != (seq & 0x0F))
            return false;
        const int take = qMin(7, total - int(acc.size()));
        if (fr.size() < 1 + take)
            return false;
        acc.append(fr.mid(1, take));
        /* ISO 15765-2：SN 为 1..F，之后为 0，再 1..（模 16 递增，不是 F 后直接回到 1） */
        seq = quint8((seq + 1u) & 0x0Fu);
    }
    acc.truncate(total);
    *responsePduOut = acc;
    return true;
}

} // namespace

namespace IsoTp {

// 仅接收一个完整响应 PDU。
bool receivePdu(CanWorker *worker,
                quint32 txId,
                quint32 rxId,
                bool ext,
                QByteArray *responsePduOut,
                int timeoutMs)
{
    if (!worker || !responsePduOut || !worker->isDeviceOpen())
        return false;
    responsePduOut->clear();
    return receiveResponsePdu(worker, txId, rxId, ext, responsePduOut, timeoutMs);
}

// 发送请求 PDU 并接收响应 PDU。
bool sendReceive(CanWorker *worker,
                 quint32 txId,
                 quint32 rxId,
                 bool ext,
                 const QByteArray &requestPdu,
                 QByteArray *responsePduOut,
                 int timeoutMs,
                 const TxFrameProgressCallback &onTxFrame)
{
    if (!worker || !responsePduOut || !worker->isDeviceOpen())
        return false;
    responsePduOut->clear();

    const int n = requestPdu.size();
    if (n < 1 || n > 4095)
        return false;

    int txFrameSeq = 0;
    const auto fireTx = [&onTxFrame, &txFrameSeq]() {
        if (onTxFrame)
            onTxFrame(++txFrameSeq);
    };

    if (n <= 7) {
        QByteArray sf(8, char(0x00));
        sf[0] = char(n & 0xFF);
        for (int i = 0; i < n; ++i)
            sf[1 + i] = requestPdu[i];
        tx(worker, txId, ext, sf);
        fireTx();
    } else {
        /* 避免队列里残留上一步的 50/71/30 等，被误当成本次流控或打乱收 0x74 的顺序 */
        worker->clearRxFifo();
        QByteArray ff(8, char(0x00));
        ff[0] = char(0x10 | ((n >> 8) & 0x0F));
        ff[1] = char(n & 0xFF);
        for (int i = 0; i < 6; ++i)
            ff[2 + i] = requestPdu[i];
        tx(worker, txId, ext, ff);
        fireTx();
        /* 首帧后给 ECU/驱动留时间再发流控；擦除或会话切换后过短易导致等不到 30h FC */
        QThread::msleep(20);

        int sent = 6;
        quint8 seq = 1;
        while (sent < n) {
            QByteArray fc;
            auto waitFc = [&]() {
                return worker->waitForIsoTpFlowControl(rxId, ext, &fc, timeoutMs);
            };
            if (!waitFc()) {
                QThread::msleep(40);
                if (!waitFc())
                    return false;
            }
            if (fc.size() < 3)
                return false;
            const unsigned st = unsigned(fc[2] & 0xFFu);
            const unsigned bs = unsigned(fc[1] & 0xFFu);
            /* ISO 15765-2：0~0x7F 为 STmin(ms)；0xF1~0xF9 为 100~900 µs */
            unsigned sepMsDelay = 0;
            unsigned sepUsDelay = 0;
            if (st <= 0x7Fu)
                sepMsDelay = st;
            else if (st >= 0xF1 && st <= 0xF9)
                sepUsDelay = (st - 0xF0u) * 100u;

            /* ISO 15765-2：收到流控后，在发第一帧连续帧之前须等待 STmin（ECU 常见 30 00 0A = 10ms） */
            if (sepUsDelay > 0)
                QThread::usleep(int(sepUsDelay));
            else if (sepMsDelay > 0)
                QThread::msleep(int(sepMsDelay));

            int blockCount = 0;
            while (sent < n && (bs == 0 || blockCount < int(bs))) {
                QByteArray cf(8, char(0x00));
                cf[0] = char(0x20 | (seq & 0x0Fu));
                const int take = qMin(7, n - sent);
                for (int i = 0; i < take; ++i)
                    cf[1 + i] = requestPdu[sent + i];
                tx(worker, txId, ext, cf);
                fireTx();
                sent += take;
                /* ISO 15765-2：连续帧 SN 1..F → 0 → 1..（模 16） */
                seq = quint8((seq + 1u) & 0x0Fu);
                ++blockCount;
                if (sent < n) {
                    if (sepUsDelay > 0)
                        QThread::usleep(int(sepUsDelay));
                    else if (sepMsDelay > 0)
                        QThread::msleep(int(sepMsDelay));
                }
            }
        }
        /*
         * 广成等 USB-CAN 在极短时间内连续 Transmit 后，末帧可能仍在驱动队列中未真正发到总线，
         * 若立刻 wait 响应，ECU 侧尚未收齐 ISO-TP 重组会迟迟不回 0x76，表现为「第 74 帧后超时」。
         * 多帧发完后短暂让出时间，再收响应（不影响 STmin，仅在本 PDU 发送结束之后）。
         */
        /* FBL 处理完多帧 0x34 后再组 0x74，偶发需 >25ms；强刷/擦除后 CPU 负载高时更长 */
        QThread::msleep(80);
    }

    return receiveResponsePdu(worker, txId, rxId, ext, responsePduOut, timeoutMs);
}

} // namespace IsoTp
