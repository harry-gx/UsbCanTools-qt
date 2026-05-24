// 文件说明：ISO-TP 传输模块，封装 UDS over CAN 的分段发送与重组接收。
#ifndef ISOTP_H
#define ISOTP_H

#include <QByteArray>
#include <functional>

class CanWorker;

namespace IsoTp {

// 已发送 CAN 帧进度回调：单帧为 1，多帧时 1=FF，后续递增。

/** 本请求已发出的第几帧 CAN 数据帧：单帧为 1；多帧时 1=FF，2 起为 CF（按发送顺序递增）。 */
using TxFrameProgressCallback = std::function<void(int frameIndex1Based)>;

/**
 * 通过 ISO-TP (ISO 15765-2) 发送一帧 UDS PDU 并接收完整响应 PDU。
 * 使用标准寻址、经典 CAN 8 字节，数据域填充至 8 字节。
 * onTxFrame 每成功发出一帧 CAN 后回调一次（可用于调试）。
 */
bool sendReceive(CanWorker *worker,
                 quint32 txId,
                 quint32 rxId,
                 bool ext,
                 const QByteArray &requestPdu,
                 QByteArray *responsePduOut,
                 int timeoutMs = 3000,
                 const TxFrameProgressCallback &onTxFrame = TxFrameProgressCallback());

// 仅接收一个完整 ISO-TP 响应（用于 0x78 后等待后续应答）。

/** 仅接收一帧完整 ISO-TP 响应（单帧或多帧），用于 0x78 之后等待 ECU 后续应答，不再发请求。 */
bool receivePdu(CanWorker *worker,
                quint32 txId,
                quint32 rxId,
                bool ext,
                QByteArray *responsePduOut,
                int timeoutMs = 3000);

} // namespace IsoTp

#endif
