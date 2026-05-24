#ifndef UDS_DIAGNOSTIC_SIMPLE_H
#define UDS_DIAGNOSTIC_SIMPLE_H

#include <QByteArray>
#include <QtGlobal>
#include <QWidget>

class AppController;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

// 轻量版 UDS 诊断面板：保留服务树、流程编辑、单发与流程执行。
class UdsDiagnosticWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UdsDiagnosticWidget(AppController *controller, QWidget *parent = nullptr);
    ~UdsDiagnosticWidget() override = default;

private slots:
    void onConnectionChanged(bool connected);
    void onServiceItemClicked(QTreeWidgetItem *item, int column);

    void onAddStep();
    void onDeleteStep();
    void onMoveStepUp();
    void onMoveStepDown();
    void onClearSteps();

    void onSendOnce();
    void onRunFlow();

    void onClearLog();
    void onResetStats();

private:
    bool sendSingleUdsPdu(const QString &name,
                          const QByteArray &req,
                          const QString &expHex,
                          bool checkResp,
                          QByteArray *respOut = nullptr);
    bool sendTransferDataChunks(const QString &name,
                                const QByteArray &req,
                                const QString &expHex,
                                bool checkResp);
    void updateTransferStateFromRequest(const QByteArray &req);
    void updateTransferStateFromResponse(const QByteArray &req, const QByteArray &resp);
    static int parseMaxBlockLengthFrom34Response(const QByteArray &resp);

    void buildUi();
    void applyStyle();
    void buildServiceTree();
    void updateSendButtons(bool connected);
    void updateStatsView();

    bool sendUds(const QString &name,
                 const QString &reqHex,
                 const QString &expHex,
                 bool checkResp);

    bool parseHexU32(const QString &text, quint32 *out) const;
    QByteArray parseHexBytes(const QString &text) const;
    QString toHexSpaced(const QByteArray &bytes) const;
    void appendLog(const QString &line);

private:
    AppController *m_controller = nullptr;

    QLineEdit *m_txIdEdit = nullptr;
    QLineEdit *m_rxIdEdit = nullptr;
    QSpinBox *m_timeoutSpin = nullptr;

    QTreeWidget *m_serviceTree = nullptr;

    QLineEdit *m_reqEdit = nullptr;
    QLineEdit *m_expEdit = nullptr;
    QPushButton *m_addStepBtn = nullptr;
    QPushButton *m_sendOnceBtn = nullptr;

    QTableWidget *m_flowTable = nullptr;
    QPushButton *m_deleteStepBtn = nullptr;
    QPushButton *m_upStepBtn = nullptr;
    QPushButton *m_downStepBtn = nullptr;
    QPushButton *m_clearStepBtn = nullptr;
    QSpinBox *m_loopSpin = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QPushButton *m_runFlowBtn = nullptr;

    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton *m_clearLogBtn = nullptr;

    QLabel *m_totalLabel = nullptr;
    QLabel *m_okLabel = nullptr;
    QLabel *m_failLabel = nullptr;
    QLabel *m_noRespLabel = nullptr;
    QPushButton *m_resetStatsBtn = nullptr;

    int m_totalCount = 0;
    int m_okCount = 0;
    int m_failCount = 0;
    int m_noRespCount = 0;

    int m_transferChunkPayload = 0;
    quint8 m_transferNextBlockSeq = 1;
};

#endif // UDS_DIAGNOSTIC_SIMPLE_H
